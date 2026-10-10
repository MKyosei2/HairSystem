#version 450
layout(location = 0) out vec4 color;
void main() {
    color = vec4(undefined_symbol, 0.0, 0.0, 1.0); // intentional compile error
}
