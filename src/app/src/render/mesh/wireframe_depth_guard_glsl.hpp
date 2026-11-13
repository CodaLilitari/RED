#pragma once

namespace sed::render
{
inline constexpr char kWireframeDepthGuardGlsl[] = R"GLSL(
// Conservative center-depth rejection for unsupported ordinary level wires.
// Bind the two sampler types to DISTINCT texture units, even when one branch
// is inactive. Snapshot must retain same-count depth samples, not resolve them.
uniform sampler2D u_wire_depth_single;
uniform sampler2DMS u_wire_depth_multi;
uniform int u_wire_depth_samples; // 0 means the single-sample texture.
uniform ivec2 u_wire_depth_extent;
uniform ivec4 u_wire_depth_viewport; // x, y, physical width, physical height.
uniform vec2 u_wire_depth_offsets[16]; // GL_SAMPLE_POSITION minus (0.5, 0.5).
uniform bool u_wire_depth_guard_ready; // Current snapshot, verified D24 format.

float wireNextUp(float value)
{
    if ( value == 0.0 )
    {
        return uintBitsToFloat(1u);
    }
    uint bits = floatBitsToUint(value);
    return uintBitsToFloat(value > 0.0 ? bits + 1u : bits - 1u);
}

float wireNextDown(float value)
{
    return -wireNextUp(-value);
}

vec2 wirePoint(float value)
{
    return vec2(value);
}

vec2 wireAdd(vec2 a, vec2 b)
{
    return vec2(wireNextDown(a.x + b.x), wireNextUp(a.y + b.y));
}

vec2 wireSubtract(vec2 a, vec2 b)
{
    return vec2(wireNextDown(a.x - b.y), wireNextUp(a.y - b.x));
}

vec2 wireMultiply(vec2 a, vec2 b)
{
    vec4 products = vec4(a.x * b.x, a.x * b.y, a.y * b.x, a.y * b.y);
    return vec2(
        wireNextDown(min(min(products.x, products.y), min(products.z, products.w))),
        wireNextUp(max(max(products.x, products.y), max(products.z, products.w))));
}

vec2 wireScale(vec2 value, float factor)
{
    return wireMultiply(value, wirePoint(factor));
}

bool wireOverlap(vec2 a, vec2 b)
{
    return a.x <= b.y && b.x <= a.y;
}

float wireSampleValue(ivec2 pixel, int sampleIndex, inout bool valid)
{
    float value;
    if ( u_wire_depth_samples > 0 )
    {
        value = texelFetch(u_wire_depth_multi, pixel, sampleIndex).r;
    }
    else
    {
        value = texelFetch(u_wire_depth_single, pixel, 0).r;
    }
    if ( isnan(value) || isinf(value) || value < 0.0 || value >= 1.0 )
    {
        valid = false;
        return 0.0;
    }
    return value;
}

vec2 wireRelativeSampleInterval(float value, float reference)
{
    // Adjacent D24 code, not a presumed round-to-nearest half-code. The
    // adjacent binary32 values also enclose texture conversion. Translate
    // around the exact fetched reference BEFORE accumulating intervals: it
    // is a coordinate origin, not a claim that it is the true surface depth.
    // This avoids repeatedly rounding ~1.0 depth sums outward by whole D24
    // codes. All operations still round outward with unchanged input bounds.
    float quantum = wireNextUp(1.0 / 16777215.0);
    vec2 conversion = wireSubtract(
        vec2(wireNextDown(value), wireNextUp(value)), wirePoint(reference));
    return wireAdd(conversion, vec2(-quantum, quantum));
}

vec2 wireSampleInterval(ivec2 pixel, int sampleIndex, float reference, inout bool valid)
{
    return wireRelativeSampleInterval(wireSampleValue(pixel, sampleIndex, valid), reference);
}

// Only call this for explicitly unsupported ORDINARY lines. Selection,
// supported-face lines and independent Thing overlays bypass it completely.
// true means discard; false means retain the existing fragment LEQUAL result.
bool wireCoherentCenterOccluded(vec2 centerNdc, float candidateDepth)
{
    if ( !u_wire_depth_guard_ready || u_wire_depth_samples < 0
        || u_wire_depth_samples > 16 || u_wire_depth_extent.x < 3
        || u_wire_depth_extent.y < 3 || isnan(candidateDepth)
        || isinf(candidateDepth) || candidateDepth < 0.0 || candidateDepth > 1.0 )
    {
        return false;
    }
    if ( any(isnan(centerNdc)) || any(isinf(centerNdc)) )
    {
        return false;
    }
    int count = max(1, u_wire_depth_samples);
    vec2 centerWindow = vec2(u_wire_depth_viewport.xy)
        + (centerNdc * 0.5 + 0.5) * vec2(u_wire_depth_viewport.zw);
    ivec2 centerPixel = ivec2(floor(centerWindow));
    if ( centerPixel.x < 1 || centerPixel.y < 1
        || centerPixel.x >= u_wire_depth_extent.x - 1
        || centerPixel.y >= u_wire_depth_extent.y - 1 )
    {
        return false; // Never clamp or duplicate missing neighborhood texels.
    }

    bool valid = true;
    float reference = wireSampleValue(centerPixel, 0, valid);
    vec2 origin = wireRelativeSampleInterval(reference, reference);
    vec2 left = wireSampleInterval(centerPixel + ivec2(-1, 0), 0, reference, valid);
    vec2 right = wireSampleInterval(centerPixel + ivec2(1, 0), 0, reference, valid);
    vec2 bottom = wireSampleInterval(centerPixel + ivec2(0, -1), 0, reference, valid);
    vec2 top = wireSampleInterval(centerPixel + ivec2(0, 1), 0, reference, valid);
    if ( !valid )
    {
        return false;
    }
    vec2 slopeX = wireScale(wireSubtract(right, left), 0.5);
    vec2 slopeY = wireScale(wireSubtract(top, bottom), 0.5);
    vec2 anchor = u_wire_depth_samples > 0 ? u_wire_depth_offsets[0] : vec2(0.0);
    vec2 meanX = vec2(0.0);
    vec2 meanY = vec2(0.0);
    for ( int sampleIndex = 0; sampleIndex < count; ++sampleIndex )
    {
        vec2 offset = u_wire_depth_samples > 0
            ? u_wire_depth_offsets[sampleIndex] : vec2(0.0);
        if ( any(isnan(offset)) || any(isinf(offset))
            || any(lessThan(offset, vec2(-0.5))) || any(greaterThan(offset, vec2(0.5))) )
        {
            return false;
        }
        meanX = wireAdd(meanX, wirePoint(offset.x));
        meanY = wireAdd(meanY, wirePoint(offset.y));
    }
    float reciprocalCount = 1.0 / float(count);
    // Source settings admit powers of two. If a driver reports another count,
    // use a reciprocal interval instead of assuming exact division below.
    if ( count != 1 && count != 2 && count != 4 && count != 8 && count != 16 )
    {
        return false;
    }
    meanX = wireScale(meanX, reciprocalCount);
    meanY = wireScale(meanY, reciprocalCount);

    vec2 averages[9];
    for ( int y = -1; y <= 1; ++y )
    {
        for ( int x = -1; x <= 1; ++x )
        {
            vec2 sum = vec2(0.0);
            for ( int sampleIndex = 0; sampleIndex < count; ++sampleIndex )
            {
                vec2 observed;
                if ( sampleIndex == 0 && x == 0 && y == 0 )
                {
                    observed = origin;
                }
                else if ( sampleIndex == 0 && x == -1 && y == 0 )
                {
                    observed = left;
                }
                else if ( sampleIndex == 0 && x == 1 && y == 0 )
                {
                    observed = right;
                }
                else if ( sampleIndex == 0 && x == 0 && y == -1 )
                {
                    observed = bottom;
                }
                else if ( sampleIndex == 0 && x == 0 && y == 1 )
                {
                    observed = top;
                }
                else
                {
                    observed = wireSampleInterval(centerPixel + ivec2(x, y), sampleIndex, reference, valid);
                }
                if ( !valid )
                {
                    return false;
                }
                vec2 offset = u_wire_depth_samples > 0
                    ? u_wire_depth_offsets[sampleIndex] : vec2(0.0);
                vec2 dx = wireAdd(wirePoint(float(x)), wireSubtract(wirePoint(offset.x), wirePoint(anchor.x)));
                vec2 dy = wireAdd(wirePoint(float(y)), wireSubtract(wirePoint(offset.y), wirePoint(anchor.y)));
                vec2 predicted = wireAdd(origin,
                    wireAdd(wireMultiply(slopeX, dx), wireMultiply(slopeY, dy)));
                if ( !wireOverlap(predicted, observed) )
                {
                    return false;
                }
                sum = wireAdd(sum, observed);
            }
            averages[(y + 1) * 3 + x + 1] = wireScale(sum, reciprocalCount);
        }
    }

    vec2 ndcX = vec2(wireNextDown(centerNdc.x), wireNextUp(centerNdc.x));
    vec2 ndcY = vec2(wireNextDown(centerNdc.y), wireNextUp(centerNdc.y));
    vec2 windowX = wireAdd(wirePoint(float(u_wire_depth_viewport.x)),
        wireScale(wireAdd(wireScale(ndcX, 0.5), wirePoint(0.5)), float(u_wire_depth_viewport.z)));
    vec2 windowY = wireAdd(wirePoint(float(u_wire_depth_viewport.y)),
        wireScale(wireAdd(wireScale(ndcY, 0.5), wirePoint(0.5)), float(u_wire_depth_viewport.w)));
    vec2 shiftedX = wireSubtract(wireSubtract(windowX, wirePoint(float(centerPixel.x) + 0.5)), meanX);
    vec2 shiftedY = wireSubtract(wireSubtract(windowY, wirePoint(float(centerPixel.y) + 0.5)), meanY);
    if ( shiftedX.x < -1.0 || shiftedX.y > 1.0 || shiftedY.x < -1.0 || shiftedY.y > 1.0 )
    {
        return false;
    }
    float sx = (shiftedX.x + shiftedX.y) * 0.5;
    float sy = (shiftedY.x + shiftedY.y) * 0.5;
    int ix = sx < 0.0 ? -1 : 0;
    int iy = sy < 0.0 ? -1 : 0;
    vec2 tx = wireSubtract(wirePoint(sx), wirePoint(float(ix)));
    vec2 ty = wireSubtract(wirePoint(sy), wirePoint(float(iy)));
    vec2 oneMinusX = wireSubtract(wirePoint(1.0), tx);
    vec2 oneMinusY = wireSubtract(wirePoint(1.0), ty);
    int index = (iy + 1) * 3 + ix + 1;
    vec2 reconstructed = wireAdd(
        wireAdd(wireMultiply(averages[index], wireMultiply(oneMinusX, oneMinusY)),
            wireMultiply(averages[index + 1], wireMultiply(tx, oneMinusY))),
        wireAdd(wireMultiply(averages[index + 3], wireMultiply(oneMinusX, ty)),
            wireMultiply(averages[index + 4], wireMultiply(tx, ty))));
    float radiusX = wireNextUp(max(abs(sx - shiftedX.x), abs(shiftedX.y - sx)));
    float radiusY = wireNextUp(max(abs(sy - shiftedY.x), abs(shiftedY.y - sy)));
    vec2 coordinateError = wireAdd(
        wireMultiply(wirePoint(max(abs(slopeX.x), abs(slopeX.y))), wirePoint(radiusX)),
        wireMultiply(wirePoint(max(abs(slopeY.x), abs(slopeY.y))), wirePoint(radiusY)));
    float upper = wireNextUp(reconstructed.y + coordinateError.y);
    vec2 candidate = wireSubtract(
        vec2(wireNextDown(candidateDepth), wireNextUp(candidateDepth)), wirePoint(reference));
    return candidate.x > upper;
}
)GLSL";
}
