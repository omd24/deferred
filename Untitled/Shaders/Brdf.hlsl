static const float Pi = 3.141592654f;

//-------------------------------------------------------------------------------------------------
// Fresnel factor using Schlick's approximation (Real-Time Rendering 4th Ed. - Eq. 9.16).
//-------------------------------------------------------------------------------------------------
float3 Fresnel(in float3 specAlbedo, in float3 h, in float3 l)
{
    float3 F0 = specAlbedo; // property of the substance
    float3 n = h; // when used as part of a BRDF, half-vector is used instead of surface normal n
    float3 fresnel = specAlbedo + (1.0f - specAlbedo) * pow((1.0f - saturate(dot(l, h))), 5.0f);

    return fresnel;
}

//-------------------------------------------------------------------------------------------------
// Beckmann geometry term (Walter, Microfacet Models for Refraction - Eq. 27).
// alpha = roughness
// nDotX = nDotv or nDotl
//-------------------------------------------------------------------------------------------------
float Beckmann_G1(in float alpha, in float nDotX)
{
    // Notes:
    // 1. TanThetha = (1 - cosThetha2) / cosThetha2
    // 2. Thetha =  angle between normal and either v or l
    // hence, cosThetha = nDotv or nDotl

    float nDotX2 = nDotX * nDotX;
    float tanTheta = sqrt((1 - nDotX2) / nDotX2);
    float a = 1.0f / (alpha * tanTheta);
    float a2 = a * a;

    float g = 1.0f;
    if(a < 1.6f)
        g *= (3.535f * a + 2.181f * a2) / (1.0f + 2.276f * a + 2.577f * a2);

    return g;
}

//-------------------------------------------------------------------------------------------------
// GGX visibility term (Walter, Microfacet Models for Refraction - Eq. 34)
//-------------------------------------------------------------------------------------------------
float GGX_G1(in float alpha2, in float nDotX)
{
    return (2.0f * nDotX) / (nDotX + sqrt(alpha2 + (1 - alpha2) * nDotX * nDotX));
}

//-------------------------------------------------------------------------------------------------
// Smith geometry term aka masking funcion (Walter, Microfacet Models for Refraction - Eq. 23),
// g1i = G1 term with incident light as X
// g1o = G1 term with view vector as X
//-------------------------------------------------------------------------------------------------
float Smith_G(in float g1i, in float g1o)
{
    return g1i * g1o;
}

//-------------------------------------------------------------------------------------------------
// Beckmann distribution term (Walter, Microfacet Models for Refraction - Eq. 25):
// alpha = roughness 
// h = half-vector (instead of microfacet normals, m, in the paper)
// n = macroscopic surface normal
//-------------------------------------------------------------------------------------------------
float3 Beckmann_D(in float alpha, in float3 n, in float3 h, in float3 v, in float3 l)
{
    float nDotH = saturate(dot(n, h));
    float nDotH2 = nDotH * nDotH;
    float nDotH4 = nDotH2 * nDotH2;
    float alpha2 = alpha * alpha;

    // Calculate the distribution term
    float tanTheta2 = (1 - nDotH2) / nDotH2;
    float expTerm = exp(-tanTheta2 / alpha2);
    return expTerm / (Pi * alpha * nDotH4);
}

//-------------------------------------------------------------------------------------------------
// GGX distribution term (Walter, Microfacet Models for Refraction - Eq. 33):
// alpha = roughness 
// h = half-vector (instead of microfacet normals, m, in the paper)
// n = macroscopic surface normal
//-------------------------------------------------------------------------------------------------
float3 GGX_D(in float alpha, in float3 n, in float3 h, in float3 v, in float3 l)
{
    float nDotH = saturate(dot(n, h));
    float nDotH2 = nDotH * nDotH;
    float nDotH4 = nDotH2 * nDotH2;
    float alpha2 = alpha * alpha;

    // The distribution term (eq rearranged to avoid extra cos4 computation)
    float d = alpha2 / (Pi * pow(nDotH4 * (alpha2 - 1) + 1, 2.0f));
    return d;
}


//
// Specular BRDF equation (Walter, Microfacet Models for Refraction - Eq. 20)
// F * G * D / (4 * nDotl * nDotv)
//

//-------------------------------------------------------------------------------------------------
// Beckmann specular Brdf
//-------------------------------------------------------------------------------------------------
float Beckmann_Specular(in float3 specularAlbedo, in float alpha, in float3 n, in float3 h, in float3 v, in float3 l)
{
    float nDotL = saturate(dot(n, l));
    float nDotV = saturate(dot(n, v));

    // distribution term:
    float d = Beckmann_D(alpha, n, h, v, l);

    // masking geometry term
    float g1i = Beckmann_G1(alpha, nDotL);
    float g1o = Beckmann_G1(alpha, nDotV);
    float g = Smith_G(g1i, g1o);

    float3 f = Fresnel(specularAlbedo, h, l);

    return f * d * g * (1.0f / (4.0f * nDotL * nDotV));
}

//-------------------------------------------------------------------------------------------------
// GGX specular Brdf
//-------------------------------------------------------------------------------------------------
float GGX_Specular(in float3 specularAlbedo, in float alpha, in float3 n, in float3 h, in float3 v, in float3 l)
{
    float nDotL = saturate(dot(n, l));
    // clamping nDotV to zero causes black pixel artifacts :sweat:
    float nDotV = max(dot(n, v), 0.0001f);

    // distribution term:
    float d = GGX_D(alpha, n, h, v, l);

    // masking geometry term
    float g1i = GGX_G1(alpha, nDotL);
    float g1o = GGX_G1(alpha, nDotV);
    float g = Smith_G(g1i, g1o);

    float3 f = Fresnel(specularAlbedo, h, l);

    return f * d * g * (1.0f / (4.0f * nDotL * nDotV));

    // N.B. 
    // The dividend of GGX_G1 helper is 2 * nDotX
    // So the dividend in the Smith G becomes 4 * nDotL * nDotV
    // Which is the divisor in GGX_Specular
    // Hence to avoid redundant computation that term can be removed from GGX_G1 dividend
    // and there would be no need to divide to (4.0f * nDotL * nDotV) here
}