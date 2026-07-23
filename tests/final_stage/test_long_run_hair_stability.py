from __future__ import annotations

import math
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HLSL = ROOT / "apps" / "dx11_viewer" / "shaders" / "HairSim.hlsl"
PART03 = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer_parts" / "03_simulation_reset_and_frame_step.inl"
HEADER = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer.h"


def _function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unbalanced function: {signature}")


def _add(a: tuple[float, float], b: tuple[float, float]) -> tuple[float, float]:
    return a[0] + b[0], a[1] + b[1]


def _sub(a: tuple[float, float], b: tuple[float, float]) -> tuple[float, float]:
    return a[0] - b[0], a[1] - b[1]


def _mul(a: tuple[float, float], scalar: float) -> tuple[float, float]:
    return a[0] * scalar, a[1] * scalar


def _length(a: tuple[float, float]) -> float:
    return math.hypot(a[0], a[1])


def _normalized(a: tuple[float, float], fallback: tuple[float, float]) -> tuple[float, float]:
    length = _length(a)
    if length <= 1.0e-12:
        return fallback
    return a[0] / length, a[1] / length


class LongRunHairStabilityContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.hlsl = HLSL.read_text(encoding="utf-8")
        cls.part03 = PART03.read_text(encoding="utf-8")
        cls.header = HEADER.read_text(encoding="utf-8")

    def test_xpbd_multipliers_are_reset_once_per_timestep(self) -> None:
        self.assertIn("void ClearConstraintLambdasCS", self.hlsl)
        self.assertIn("DistanceLambdas[constraintIndex] = 0.0f", self.hlsl)
        self.assertIn("BendLambdas[constraintIndex] = 0.0f", self.hlsl)
        clear_dispatch = self.part03.index("m_clearConstraintLambdasCS.Get()")
        solver_loop = self.part03.index("for (uint32_t iteration = 0u;")
        self.assertLess(clear_dispatch, solver_loop)

    def test_static_root_preserves_authored_scalp_lift(self) -> None:
        body = _function_body(self.hlsl, "void RootBindCS(")
        self.assertIn("float3 restRoot = RestPositions[rootIndex].xyz", body)
        self.assertIn("float3 liftedScalpRoot", body)
        self.assertIn("scalpRoot + scalpNormal * authoredLift", body)
        self.assertIn("liftedScalpRoot,", body)

    def test_bend_constraint_is_rest_chord_and_not_direction_free_offset(self) -> None:
        body = _function_body(self.hlsl, "void BendCS(")
        self.assertIn("targetChordLength", body)
        self.assertIn("length(restC - restA)", body)
        self.assertNotIn("ComputeAngularTargetOffset", body)
        self.assertNotIn("b - midpoint", body)

    def test_stability_guard_runs_before_followers(self) -> None:
        self.assertNotIn("void StretchGuardCS", self.hlsl)
        self.assertIn("void StabilityGuardCS", self.hlsl)
        guard_dispatch = self.part03.index("m_stabilityGuardCS.Get()")
        follow_dispatch = self.part03.index("m_followCS.Get()")
        self.assertLess(guard_dispatch, follow_dispatch)
        self.assertIn("enableCatastrophicRecovery", self.header)
        self.assertIn("IsExperimentModeEnabled() ? 0u : 1u", self.part03)

    def test_followers_cannot_expand_an_invalid_parent_guide(self) -> None:
        body = _function_body(self.hlsl, "void FollowCS(")
        self.assertIn("uint(follow.ParentGuide) >= GuideStrandCount", body)
        self.assertIn("maximumFollowerRadius", body)
        self.assertIn("target = translatedRest", body)
        self.assertIn("!IsFinite3(parentPosition)", body)
        self.assertIn("previousFollowerRoot", body)

    def test_cpu_analogue_remains_finite_and_inextensible(self) -> None:
        # Small deterministic analogue of the shader ordering. It exercises 128 points,
        # 6 solver iterations, per-step lambda clearing, rest-chord bending and the
        # final root-to-tip extension guard for a long sequence.
        point_count = 128
        strand_length = 0.44
        segment = strand_length / (point_count - 1)
        rest = [(0.0, -segment * index) for index in range(point_count)]
        current = list(rest)
        previous = list(rest)
        dt = 1.0 / 60.0
        damping = 0.065
        gravity = 9.81

        for frame in range(1200):
            output = [current[0]] * point_count
            output_previous = [current[0]] * point_count
            output[0] = rest[0]
            output_previous[0] = rest[0]
            lateral = 0.8 * math.sin(frame * 0.011)
            for index in range(1, point_count):
                velocity = _mul(_sub(current[index], previous[index]), 1.0 - damping)
                acceleration = (lateral * (index / (point_count - 1)), -gravity)
                output[index] = _add(_add(current[index], velocity), _mul(acceleration, dt * dt))
                output_previous[index] = current[index]

            distance_lambda = [0.0] * (point_count - 1)
            bend_lambda = [0.0] * (point_count - 2)
            for _ in range(6):
                for index in range(1, point_count):
                    a_index = index - 1
                    b_index = index
                    delta = _sub(output[b_index], output[a_index])
                    direction = _normalized(delta, (0.0, -1.0))
                    current_length = max(_length(delta), 1.0e-9)
                    constraint = current_length - segment
                    w_a = 0.0 if index == 1 else 1.0
                    d_lambda = -constraint / (w_a + 1.0)
                    distance_lambda[index - 1] += d_lambda
                    if w_a > 0.0:
                        correction = _mul(direction, -w_a * d_lambda)
                        output[a_index] = _add(output[a_index], correction)
                        output_previous[a_index] = _add(output_previous[a_index], correction)
                    correction = _mul(direction, d_lambda)
                    output[b_index] = _add(output[b_index], correction)
                    output_previous[b_index] = _add(output_previous[b_index], correction)

                for index in range(1, point_count - 1):
                    a_index = index - 1
                    c_index = index + 1
                    chord = _sub(output[c_index], output[a_index])
                    rest_chord = _sub(rest[c_index], rest[a_index])
                    direction = _normalized(chord, _normalized(rest_chord, (0.0, -1.0)))
                    constraint = _length(chord) - _length(rest_chord)
                    w_a = 0.0 if index == 1 else 1.0
                    d_lambda = -constraint / (w_a + 1.0)
                    bend_lambda[index - 1] += d_lambda
                    gain = 0.35 * (1.0 - 0.5 * 0.08)
                    if w_a > 0.0:
                        correction = _mul(direction, -w_a * d_lambda * gain)
                        output[a_index] = _add(output[a_index], correction)
                        output_previous[a_index] = _add(output_previous[a_index], correction)
                    correction = _mul(direction, d_lambda * gain)
                    output[c_index] = _add(output[c_index], correction)
                    output_previous[c_index] = _add(output_previous[c_index], correction)

            for index in range(1, point_count):
                delta = _sub(output[index], output[index - 1])
                length = _length(delta)
                if length > segment * 1.005:
                    direction = _normalized(delta, (0.0, -1.0))
                    corrected = _add(output[index - 1], _mul(direction, segment))
                    correction = _sub(corrected, output[index])
                    output[index] = corrected
                    output_previous[index] = _add(output_previous[index], correction)

            previous, current = output_previous, output

        max_segment_ratio = 0.0
        for index in range(1, point_count):
            self.assertTrue(math.isfinite(current[index][0]))
            self.assertTrue(math.isfinite(current[index][1]))
            max_segment_ratio = max(
                max_segment_ratio,
                _length(_sub(current[index], current[index - 1])) / segment,
            )
        self.assertLessEqual(max_segment_ratio, 1.006)
        self.assertLess(_length(_sub(current[-1], current[0])), 0.55)


if __name__ == "__main__":
    unittest.main()
