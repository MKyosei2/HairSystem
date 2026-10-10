float4 frag() : SV_Target
{
    // Intentional Bug Zoo regression: red output instead of the golden neutral color.
    return float4(1.0, 0.0, 0.0, 1.0);
}
