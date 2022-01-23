#version 100

precision mediump float;
precision mediump sampler2D;
uniform mediump vec4 uleftBorderColor_Stage1_c0_c0;
uniform mediump vec4 urightBorderColor_Stage1_c0_c0;
uniform mediump vec4 ustart_Stage1_c0_c0_c1_c0;
uniform mediump vec4 uend_Stage1_c0_c0_c1_c0;
uniform sampler2D uTextureSampler_0_Stage2;
varying highp vec2 vTransformedCoords_0_Stage0;
varying highp vec2 vTransformedCoords_1_Stage0;
varying mediump vec4 vcolor_Stage0;
mediump vec4 stage_Stage1_c0_c0_c0_c0(mediump vec4 _input) {
    mediump vec4 _sample1099_c0_c0;
    mediump float t = length(vTransformedCoords_0_Stage0);
    _sample1099_c0_c0 = vec4(t, 1.0, 0.0, 0.0);
    return _sample1099_c0_c0;
}
mediump vec4 stage_Stage1_c0_c0_c1_c0(mediump vec4 _input) {
    mediump vec4 _sample1767_c0_c0;
    mediump float t = _input.x;
    _sample1767_c0_c0 = (1.0 - t) * ustart_Stage1_c0_c0_c1_c0 + t * uend_Stage1_c0_c0_c1_c0;
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
    return child;
}
void main() {
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
        output_Stage2 = texture2D(uTextureSampler_0_Stage2, vTransformedCoords_1_Stage0).wwww;
    }
    {
        gl_FragColor = output_Stage1 * output_Stage2;
    }
}
