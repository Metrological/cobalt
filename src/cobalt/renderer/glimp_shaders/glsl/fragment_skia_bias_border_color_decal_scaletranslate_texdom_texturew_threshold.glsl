#version 100

uniform highp float u_skRTHeight;
precision mediump float;
precision mediump sampler2D;
uniform mediump vec4 uleftBorderColor_Stage1_c0_c0;
uniform mediump vec4 urightBorderColor_Stage1_c0_c0;
uniform highp vec4 uscale01_Stage1_c0_c0_c1_c0;
uniform highp vec4 ubias01_Stage1_c0_c0_c1_c0;
uniform highp vec4 uscale23_Stage1_c0_c0_c1_c0;
uniform highp vec4 ubias23_Stage1_c0_c0_c1_c0;
uniform mediump float uthreshold_Stage1_c0_c0_c1_c0;
uniform mediump vec4 uscaleAndTranslate_Stage2;
uniform mediump vec4 uTexDom_Stage2;
uniform mediump vec3 uDecalParams_Stage2;
uniform sampler2D uTextureSampler_0_Stage2;
varying mediump vec4 vcolor_Stage0;
varying highp vec2 vTransformedCoords_0_Stage0;
mediump vec4 stage_Stage1_c0_c0_c0_c0(mediump vec4 _input) {
    mediump vec4 _sample1099_c0_c0;
    mediump float t = vTransformedCoords_0_Stage0.x + 9.9999997473787516e-06;
    _sample1099_c0_c0 = vec4(t, 1.0, 0.0, 0.0);
    return _sample1099_c0_c0;
}
mediump vec4 stage_Stage1_c0_c0_c1_c0(mediump vec4 _input) {
    mediump vec4 _sample1767_c0_c0;
    mediump float t = _input.x;
    highp vec4 scale, bias;
    if (t < uthreshold_Stage1_c0_c0_c1_c0) {
        scale = uscale01_Stage1_c0_c0_c1_c0;
        bias = ubias01_Stage1_c0_c0_c1_c0;
    } else {
        scale = uscale23_Stage1_c0_c0_c1_c0;
        bias = ubias23_Stage1_c0_c0_c1_c0;
    }
    _sample1767_c0_c0 = t * scale + bias;
    return _sample1767_c0_c0;
}
mediump vec4 stage_Stage1_c0_c0(mediump vec4 _input) {
    mediump vec4 child;
    mediump vec4 _sample1099_c0_c0;
    _sample1099_c0_c0 = stage_Stage1_c0_c0_c0_c0(vec4(1.0));
    mediump vec4 t = _sample1099_c0_c0;
    if (t.x < 0.0) {
        child = uleftBorderColor_Stage1_c0_c0;
    } else if (t.x > 1.0) {
        child = urightBorderColor_Stage1_c0_c0;
    } else {
        mediump vec4 _sample1767_c0_c0;
        _sample1767_c0_c0 = stage_Stage1_c0_c0_c1_c0(t);
        child = _sample1767_c0_c0;
    }
    {
        child.xyz *= child.w;
    }
    return child;
}
void main() {
highp     vec4 sk_FragCoord = vec4(gl_FragCoord.x, u_skRTHeight - gl_FragCoord.y, gl_FragCoord.z, gl_FragCoord.w);
    mediump vec4 outputColor_Stage0;
    {
        outputColor_Stage0 = vcolor_Stage0;
    }
    mediump vec4 output_Stage1;
    {
        mediump vec4 child;
        child = stage_Stage1_c0_c0(vec4(1.0));
        output_Stage1 = child * outputColor_Stage0.w;
    }
    mediump vec4 output_Stage2;
    {
        mediump vec2 coords = sk_FragCoord.xy * uscaleAndTranslate_Stage2.xy + uscaleAndTranslate_Stage2.zw;
        {
            highp vec2 origCoord = coords;
            highp vec2 clampedCoord = clamp(origCoord, uTexDom_Stage2.xy, uTexDom_Stage2.zw);
            mediump vec4 inside = texture2D(uTextureSampler_0_Stage2, clampedCoord).wwww;
            mediump float err = max(abs(clampedCoord.x - origCoord.x) * uDecalParams_Stage2.x, abs(clampedCoord.y - origCoord.y) * uDecalParams_Stage2.y);
            if (err > uDecalParams_Stage2.z) {
                err = 1.0;
            } else if (uDecalParams_Stage2.z < 1.0) {
                err = 0.0;
            }
            output_Stage2 = mix(inside, vec4(0.0, 0.0, 0.0, 0.0), err);
        }
    }
    {
        gl_FragColor = output_Stage1 * output_Stage2;
    }
}
