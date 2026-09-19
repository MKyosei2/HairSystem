from __future__ import annotations
import argparse, csv, json, math, statistics, struct
from collections import defaultdict
from pathlib import Path
from typing import Any

COUNTERS = {
    "dynamic": "counter_0", "static_reuse": "counter_1", "repair": "counter_3",
    "follower_rebuilt_points": "counter_43", "render_regenerated_vertices": "counter_44",
    "dependency_fallback": "counter_50", "render_fallback": "counter_51",
    "transition_bytes": "counter_52", "payload_read_bytes": "counter_55",
    "payload_write_bytes": "counter_56",
}

def read_csv(p: Path):
    with p.open("r", encoding="utf-8-sig", newline="") as f: return list(csv.DictReader(f))

def fnum(row, *names):
    for n in names:
        raw = row.get(n)
        if raw in (None, ""): continue
        try: v = float(raw)
        except (TypeError, ValueError): continue
        if math.isfinite(v): return v
    return None

def inum(row, *names):
    v = fnum(row, *names); return None if v is None else int(v)

def pct(vals, q):
    vals = sorted(float(v) for v in vals if v is not None and math.isfinite(float(v)))
    if not vals: return None
    return vals[min(len(vals)-1, max(0, math.ceil(len(vals)*q)-1))]

def med(vals):
    vals = [float(v) for v in vals if v is not None and math.isfinite(float(v))]
    return statistics.median(vals) if vals else None

def frame_no(row):
    return inum(row, "globalFrameIndex", "sourceFrameIndex", "measuredFrameIndex", "frame", "source_frame")

def window(rows, start, size):
    return [r for r in rows if frame_no(r) is not None and start <= frame_no(r) <= start+size]

def partner(edit: Path, suffix: str):
    stem = edit.name.replace("_thesis_edits.csv", "")
    exact = edit.with_name(stem + suffix)
    if exact.is_file(): return exact
    c = sorted(edit.parent.glob("*" + suffix)); return c[0] if len(c) == 1 else None

def vmax(rows, name):
    vals = [fnum(r, name) for r in rows]; vals = [v for v in vals if v is not None]
    return max(vals) if vals else None

def vlast(rows, *names):
    vals = [fnum(r, *names) for r in rows]; vals = [v for v in vals if v is not None]
    return vals[-1] if vals else None

def bmp(path: Path):
    raw = path.read_bytes()
    if len(raw) < 54 or raw[:2] != b"BM": raise ValueError("not BMP")
    off = struct.unpack_from("<I", raw, 10)[0]; w = struct.unpack_from("<i", raw, 18)[0]
    sh = struct.unpack_from("<i", raw, 22)[0]; bpp = struct.unpack_from("<H", raw, 28)[0]
    comp = struct.unpack_from("<I", raw, 30)[0]
    if w <= 0 or sh == 0 or bpp not in (24,32) or comp != 0: raise ValueError("unsupported BMP")
    h = abs(sh); top = sh < 0; bppb = bpp//8; stride = ((w*bpp+31)//32)*4; px=[]
    for y in range(h):
        sy = y if top else h-1-y; base = off + sy*stride
        for x in range(w):
            pos=base+x*bppb; b,g,r=raw[pos:pos+3]; px.append((r,g,b))
    return w,h,px

def compare_bmp(a,b):
    aw,ah,ap=bmp(a); bw,bh,bp=bmp(b)
    if (aw,ah)!=(bw,bh): return {"comparable":False,"reason":"dimension_mismatch","a":str(a),"b":str(b)}
    s2=sa=0.0; mx=0; n=len(ap)*3
    for pa,pb in zip(ap,bp):
        for va,vb in zip(pa,pb):
            d=abs(va-vb); sa+=d; s2+=d*d; mx=max(mx,d)
    return {"comparable":True,"width":aw,"height":ah,
            "meanAbsoluteChannelError":sa/n if n else None,
            "rmsChannelError":math.sqrt(s2/n) if n else None,
            "maxChannelError":mx,"a":str(a),"b":str(b)}

def visual_pairs(root):
    by={}
    for p in root.rglob("*_visual.bmp"):
        if "_B0_" in p.name: k=str(p.parent)+"::"+p.name.replace("_B0_","_METHOD_"); by.setdefault(k,{})["B0"]=p
        elif "_P_" in p.name: k=str(p.parent)+"::"+p.name.replace("_P_","_METHOD_"); by.setdefault(k,{})["P"]=p
    out=[]
    for k,pair in sorted(by.items()):
        if "B0" in pair and "P" in pair:
            try: r=compare_bmp(pair["B0"],pair["P"])
            except Exception as e: r={"comparable":False,"reason":str(e),"a":str(pair["B0"]),"b":str(pair["P"])}
            r["pairKey"]=k; out.append(r)
    return out

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--input",required=True,type=Path); ap.add_argument("--output",required=True,type=Path); ap.add_argument("--window",type=int,default=8)
    a=ap.parse_args(); a.output.mkdir(parents=True,exist_ok=True); episodes=[]; gaps=[]
    for ef in sorted(a.input.rglob("*_thesis_edits.csv")):
        ff=partner(ef,"_frames.csv"); nf=partner(ef,"_novelty01.csv"); fr=read_csv(ff) if ff else []; nr=read_csv(nf) if nf else []
        for e in read_csv(ef):
            start=inum(e,"frame")
            if start is None: gaps.append({"editFile":str(ef),"episode":e.get("episode"),"reason":"missing_episode_frame"}); continue
            fw=window(fr,start,a.window); nw=window(nr,start,a.window); method=str(e.get("method_label","")).upper(); total=inum(e,"total_group_count") or 0; direct=inum(e,"direct_group_count") or 0
            dyn=vmax(nw,COUNTERS["dynamic"]); rep=vmax(nw,COUNTERS["repair"])
            if method=="B0" and total>0: updated=float(total); source="B0_full_dynamic_contract"
            elif dyn is not None or rep is not None: updated=min(float(total) if total>0 else math.inf,float(dyn or 0)+float(rep or 0)); source="counter_dynamic_plus_repair"
            else: updated=None; source="missing_group_execution_evidence"
            ms=[v for r in fw for v in [fnum(r,"frameIntervalMilliseconds","renderFrameMilliseconds","frameMilliseconds")] if v is not None]
            follower_gpu=[v for r in fw for v in [fnum(r,"followerReconstructionGpuMilliseconds","follower_reconstruction_gpu_ms")] if v is not None]
            render_prep_gpu=[v for r in fw for v in [fnum(r,"renderPreparationGpuMilliseconds","render_preparation_gpu_ms")] if v is not None]
            edit_to_present=fnum(e,"edit_to_first_present_ms","editToFirstPresentMilliseconds")
            first_present_observed=inum(e,"first_present_observed","firstPresentObserved")
            row=dict(e); row.update({"edit_file":str(ef),"frame_csv":str(ff) if ff else "","novelty_csv":str(nf) if nf else "","updated_group_count":updated,"updated_group_source":source,
                "update_ratio":updated/total if updated is not None and total else None,"reuse_ratio":1-updated/total if updated is not None and total else None,"update_amplification":updated/direct if updated is not None and direct else None,
                "edit_to_first_present_ms":edit_to_present,"first_present_observed":first_present_observed,
                "post_edit_frame_median_ms":med(ms),"post_edit_frame_p95_ms":pct(ms,.95),"post_edit_frame_p99_ms":pct(ms,.99),"post_edit_frame_max_ms":max(ms) if ms else None,"post_edit_over_16_6667_ratio":sum(v>16.6667 for v in ms)/len(ms) if ms else None,
                "follower_reconstruction_gpu_median_ms":med(follower_gpu),"follower_reconstruction_gpu_p95_ms":pct(follower_gpu,.95),
                "render_preparation_gpu_median_ms":med(render_prep_gpu),"render_preparation_gpu_p95_ms":pct(render_prep_gpu,.95),
                "position_rmse_mm":vlast(nw,"position_rmse_mm"),"position_max_mm":vlast(nw,"position_max_mm"),"follower_error_mm":vlast(nw,"follower_error_mm"),"render_error_mm":vlast(nw,"render_error_mm"),"ground_truth_samples":vlast(nw,"ground_truth_samples"),
                "state_sequence_hash":vlast(fw,"stateSequenceHash"),"compaction_sequence_hash":vlast(fw,"compactionSequenceHash"),"final_position_hash":vlast(fw,"finalPositionHash")})
            for label,col in COUNTERS.items(): row[label+"_max"]=vmax(nw,col)
            episodes.append(row)
            if not fw: gaps.append({"editFile":str(ef),"episode":e.get("episode"),"reason":"missing_frame_window"})
            if method=="P" and not nw: gaps.append({"editFile":str(ef),"episode":e.get("episode"),"reason":"missing_novelty_window"})
            if updated is None: gaps.append({"editFile":str(ef),"episode":e.get("episode"),"reason":source})
    parity=[]; hashes=defaultdict(lambda:defaultdict(set))
    for r in episodes:
        h=str(r.get("physical_edit_hash","")).strip()
        if h: hashes[(str(r.get("case","")),str(r.get("episode","")))][str(r.get("method_label","")).upper()].add(h)
    for (case,ep),m in hashes.items():
        if "B0" in m and "P" in m and m["B0"]!=m["P"]: parity.append({"case":case,"episode":ep,"B0":sorted(m["B0"]),"P":sorted(m["P"])})
    groups=defaultdict(list)
    for r in episodes: groups[(str(r.get("case","")),str(r.get("method_label","")))].append(r)
    ag=[]
    for (case,method),rs in sorted(groups.items()):
        def vals(k): return [r[k] for r in rs if isinstance(r.get(k),(int,float)) and math.isfinite(float(r[k]))]
        ag.append({"case":case,"method":method,"episode_count":len(rs),"median_update_ratio":med(vals("update_ratio")),"median_reuse_ratio":med(vals("reuse_ratio")),"median_update_amplification":med(vals("update_amplification")),"median_edit_to_first_present_ms":med(vals("edit_to_first_present_ms")),"p95_edit_to_first_present_ms":pct(vals("edit_to_first_present_ms"),.95),"median_post_edit_frame_ms":med(vals("post_edit_frame_median_ms")),"p95_post_edit_frame_ms":pct(vals("post_edit_frame_p95_ms"),.95),"median_follower_reconstruction_gpu_ms":med(vals("follower_reconstruction_gpu_median_ms")),"p95_follower_reconstruction_gpu_ms":pct(vals("follower_reconstruction_gpu_p95_ms"),.95),"median_render_preparation_gpu_ms":med(vals("render_preparation_gpu_median_ms")),"p95_render_preparation_gpu_ms":pct(vals("render_preparation_gpu_p95_ms"),.95),"median_follower_rebuilt_points":med(vals("follower_rebuilt_points_max")),"median_render_regenerated_vertices":med(vals("render_regenerated_vertices_max")),"median_position_rmse_mm":med(vals("position_rmse_mm"))})
    epcsv=a.output/"thesis_practical_episode_summary.csv"; cols=sorted({k for r in episodes for k in r})
    with epcsv.open("w",encoding="utf-8",newline="") as f: w=csv.DictWriter(f,fieldnames=cols); w.writeheader(); w.writerows(episodes)
    agcsv=a.output/"thesis_practical_case_method_summary.csv"; cols2=sorted({k for r in ag for k in r})
    with agcsv.open("w",encoding="utf-8",newline="") as f: w=csv.DictWriter(f,fieldnames=cols2); w.writeheader(); w.writerows(ag)
    vis=visual_pairs(a.input); visp=a.output/"thesis_practical_visual_pairs.json"; visp.write_text(json.dumps(vis,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    summary={"schema":"hairgpu-thesis-practical-report-v3","episodeCount":len(episodes),"caseMethodRowCount":len(ag),"physicalEditParityIssueCount":len(parity),"evidenceGapCount":len(gaps),"visualPairCount":len(vis),"passedInputParity":not parity,"passedEvidenceCompleteness":bool(episodes) and not gaps,"physicalEditParityIssues":parity,"evidenceGaps":gaps,"episodeCsv":str(epcsv),"caseMethodCsv":str(agcsv),"visualPairsJson":str(visp)}
    (a.output/"thesis_practical_report.json").write_text(json.dumps(summary,ensure_ascii=False,indent=2)+"\n",encoding="utf-8"); print(json.dumps(summary,ensure_ascii=False,indent=2)); return 0 if not parity else 2
if __name__=="__main__": raise SystemExit(main())
