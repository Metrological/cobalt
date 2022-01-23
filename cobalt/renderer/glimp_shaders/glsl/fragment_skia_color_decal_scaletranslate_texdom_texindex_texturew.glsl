#version 100

uniform highp float u_skRTHeight;
precision mediump float;
precision mediump sampler2D;
uniform mediump vec4 uscaleAndTranslate_Stage1;
uniform mediump vec4 uTexDom_Stage1;
uniform mediump vec3 uDecalParams_Stage1;
uniform sampler2D uTextureSampler_0_Stage0;
uniform sampler2D uTextureSampler_0_Stage1;
varying highp vec2 vTextureCoords_Stage0;
varying highp float vTexIndex_Stage0;
varying mediump vec4 vinColor_Stage0;
void main() {
highp     vec4 sk_FragCoord = vec4(gl_FragCoord.x, u_skRTHeight - gl_FragCoord.y, gl_FragCoord.z, gl_FragCoord.w);
    mediump vec4 outputColor_Stage0;
    mediump vec4 outputCoverage_Stage0;
    {
        outputColor_Stage0 = vinColor_Stage0;
        mediump vec4 texColor;
        {
            texColor = texture2D(uTextureSampler_0_Stage0, vTextureCoords_Stage0).wwww;
        }
        outputCoverage_Stage0 = texColor;
    }
    mediump vec4 output_Stage1;
    {
        mediump vec2 coords = sk_FragCoord.xy * uscaleAndTranslate_Stage1.xy + uscaleAndTranslate_Stage1.zw;
        {
            highp vec2 origCoord = coords;
            highp vec2 clampedCoord = clamp(origCoord, uTexDom_Stage1.xy, uTexDom_Stage1.zw);
            mediump vec4 inside = texture2D(uTextureSampler_0_Stage1, clampedCoord).wwww * outputCoverage_Stage0;
            mediump float err = max(abs(clampedCoord.x - origCoord.x) * uDecalParams_Stage1.x, abs(clampedCoord.y - origCoord.y) * uDecalParams_Stage1.y);
            if (err > uDecalParams_Stage1.z) {
                err = 1.0;
            } else if (uDecalParams_Stage1.z < 1.0) {
                err = 0.0;
            }
            output_Stage1 = mix(inside, vec4(0.0, 0.0, 0.0, 0.0), err);
        }
    }
    {
        gl_FragColor = outputColor_Stage0 * output_Stage1;
    }
}
