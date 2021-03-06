#pragma once

#include <glm/glm.hpp>
#include <glm/gtx/intersect.hpp>

#include "sceneStructs.h"
#include "utilities.h"

/**
 * Handy-dandy hash function that provides seeds for random number generation.
 */
__host__ __device__ inline unsigned int utilhash(unsigned int a) {
    a = (a + 0x7ed55d16) + (a << 12);
    a = (a ^ 0xc761c23c) ^ (a >> 19);
    a = (a + 0x165667b1) + (a << 5);
    a = (a + 0xd3a2646c) ^ (a << 9);
    a = (a + 0xfd7046c5) + (a << 3);
    a = (a ^ 0xb55a4f09) ^ (a >> 16);
    return a;
}

// CHECKITOUT
/**
 * Compute a point at parameter value `t` on ray `r`.
 * Falls slightly short so that it doesn't intersect the object it's hitting.
 */
__host__ __device__ glm::vec3 getPointOnRay(Ray r, float t) {
    return r.origin + (t - .0001f) * glm::normalize(r.direction);
}

/**
 * Multiplies a mat4 and a vec4 and returns a vec3 clipped from the vec4.
 */
__host__ __device__ glm::vec3 multiplyMV(glm::mat4 m, glm::vec4 v) {
    return glm::vec3(m * v);
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed cube. Untransformed,
 * the cube ranges from -0.5 to 0.5 in each axis and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float boxIntersectionTest(Geom box, Ray r,
        glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    Ray q;
    q.origin    =                multiplyMV(box.inverseTransform, glm::vec4(r.origin   , 1.0f));
    q.direction = glm::normalize(multiplyMV(box.inverseTransform, glm::vec4(r.direction, 0.0f)));

    float tmin = -1e38f;
    float tmax = 1e38f;
    glm::vec3 tmin_n;
    glm::vec3 tmax_n;
    for (int xyz = 0; xyz < 3; ++xyz) {
        float qdxyz = q.direction[xyz];
        /*if (glm::abs(qdxyz) > 0.00001f)*/ {
            float t1 = (-0.5f - q.origin[xyz]) / qdxyz;
            float t2 = (+0.5f - q.origin[xyz]) / qdxyz;
            float ta = glm::min(t1, t2);
            float tb = glm::max(t1, t2);
            glm::vec3 n;
            n[xyz] = t2 < t1 ? +1 : -1;
            if (ta > 0 && ta > tmin) {
                tmin = ta;
                tmin_n = n;
            }
            if (tb < tmax) {
                tmax = tb;
                tmax_n = n;
            }
        }
    }

    if (tmax >= tmin && tmax > 0) {
        outside = true;
        if (tmin <= 0) {
            tmin = tmax;
            tmin_n = tmax_n;
            outside = false;
        }
        intersectionPoint = multiplyMV(box.transform, glm::vec4(getPointOnRay(q, tmin), 1.0f));
        normal = glm::normalize(multiplyMV(box.transform, glm::vec4(tmin_n, 0.0f)));
        return glm::length(r.origin - intersectionPoint);
    }
    return -1;
}

// CHECKITOUT
/**
 * Test intersection between a ray and a transformed sphere. Untransformed,
 * the sphere always has radius 0.5 and is centered at the origin.
 *
 * @param intersectionPoint  Output parameter for point of intersection.
 * @param normal             Output parameter for surface normal.
 * @param outside            Output param for whether the ray came from outside.
 * @return                   Ray parameter `t` value. -1 if no intersection.
 */
__host__ __device__ float sphereIntersectionTest(Geom sphere, Ray r,
        glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside) {
    float radius = .5;

    glm::vec3 ro = multiplyMV(sphere.inverseTransform, glm::vec4(r.origin, 1.0f));
    glm::vec3 rd = glm::normalize(multiplyMV(sphere.inverseTransform, glm::vec4(r.direction, 0.0f)));

    Ray rt;
    rt.origin = ro;
    rt.direction = rd;

    float vDotDirection = glm::dot(rt.origin, rt.direction);
    float radicand = vDotDirection * vDotDirection - (glm::dot(rt.origin, rt.origin) - powf(radius, 2));
    if (radicand < 0) {
        return -1;
    }

    float squareRoot = sqrt(radicand);
    float firstTerm = -vDotDirection;
    float t1 = firstTerm + squareRoot;
    float t2 = firstTerm - squareRoot;

    float t = 0;
    if (t1 < 0 && t2 < 0) {
        return -1;
    } else if (t1 > 0 && t2 > 0) {
        t = min(t1, t2);
        outside = true;
    } else {
        t = max(t1, t2);
        outside = false;
    }

    glm::vec3 objspaceIntersection = getPointOnRay(rt, t);

    intersectionPoint = multiplyMV(sphere.transform, glm::vec4(objspaceIntersection, 1.f));
    normal = glm::normalize(multiplyMV(sphere.invTranspose, glm::vec4(objspaceIntersection, 0.f)));
    if (!outside) {
        normal = -normal;
    }

    return glm::length(r.origin - intersectionPoint);
}



__host__ __device__ float csg1SDF(glm::vec3 p)
{
    float x4 = p.x * p.x * p.x * p.x;
    float x2 = p.x * p.x;
    float y4 = p.y * p.y * p.y * p.y;
    float y2 = p.y * p.y;
    float z4 = p.z * p.z * p.z * p.z;
    float z2 = p.z * p.z;
    return x4 - 5 * x2 + y4 - 5 * y2 + z4 - 5 * z2 + 11.8;
}

__host__ __device__ float csg1Raytrace(glm::vec3 cam, glm::vec3 ray, float maxdist, bool &outside) 
{
    float BIGSTEPSIZE = 0.1;
    float SMALLSTEPSIZE = 0.02;
    float t = 0.0f;
    float step = BIGSTEPSIZE;

    for (int i = 0; i < 700; i++) {
        glm::vec3 p = cam + ray * t;
        float distance = csg1SDF(p);

        if (distance < 0.001) {

            //step back once, and increment slowly
            t -= step;
            step = SMALLSTEPSIZE;

            for (int i = 0; i < 10; i++) 
            {
                p = cam + ray * t;
                distance = csg1SDF(p);
                if (distance < 0.001) {
                    t -= step;
                    p = cam + ray * t;
                    // InitializeIntersection(isect, t, p);
                    return t;
                }
                t += step;
            }
            return 0;
        }
        t += step;
    }
    return 0;
}

__host__ __device__ glm::vec3 getCsg1Normal(glm::vec3 p)
{
    return glm::normalize(glm::vec3(
        csg1SDF(glm::vec3(p.x + EPSILON, p.y, p.z)) - csg1SDF(glm::vec3(p.x - EPSILON, p.y, p.z)),
        csg1SDF(glm::vec3(p.x, p.y + EPSILON, p.z)) - csg1SDF(glm::vec3(p.x, p.y - EPSILON, p.z)),
        csg1SDF(glm::vec3(p.x, p.y, p.z + EPSILON)) - csg1SDF(glm::vec3(p.x, p.y, p.z - EPSILON))
    ));
}

__host__ __device__ float csg1IntersectionTest(Geom surface, Ray r,
    glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside)
{
    glm::vec3 pt = multiplyMV(surface.inverseTransform, glm::vec4(r.origin, 1.0f));
    glm::vec3 dir = multiplyMV(surface.inverseTransform, glm::vec4(r.direction, 0.0f));

    // raytrace to get t value
    float t = csg1Raytrace(pt, dir, 100.f, outside);

    // calculate point on surface using ray and t value
    glm::vec3 objPt = pt + t * dir;
    intersectionPoint = multiplyMV(surface.transform, glm::vec4(objPt, 1.0f));

    // calculate normal using gradient
    normal = glm::normalize(multiplyMV(surface.invTranspose, glm::vec4(getCsg1Normal(objPt), 0.0f)));
    if (!outside) normal = -normal;
    return t;
}



__host__ __device__ float csg2SDF(glm::vec3 p)
{    
    float k = 5.0;
    float a = 0.95;
    float b = 0.5;

    float x2 = p.x * p.x;
    float y2 = p.y * p.y;
    float z2 = p.z * p.z;
    return (x2 + y2 + z2 - a*k*k) *  (x2 + y2 + z2 - a*k*k) - b * ((p.z - k)*(p.z - k) - 2*p.x*p.x) * ((p.z + k)*(p.z + k) - 2 *p.y*p.y);
}

__host__ __device__ float csg2Raytrace(glm::vec3 cam, glm::vec3 ray, float maxdist, bool &outside)
{
    float BIGSTEPSIZE = 0.1;
    float SMALLSTEPSIZE = 0.02;
    float t = 0.0f;
    float step = BIGSTEPSIZE;

    for (int i = 0; i < 700; i++) {
        glm::vec3 p = cam + ray * t;
        float distance = csg2SDF(p);

        if (distance < 0.001) {

            //step back once, and increment slowly
            t -= step;
            step = SMALLSTEPSIZE;

            for (int i = 0; i < 10; i++)
            {
                p = cam + ray * t;
                distance = csg2SDF(p);
                if (distance < 0.001) {
                    t -= step;
                    p = cam + ray * t;
                    // InitializeIntersection(isect, t, p);
                    return t;
                }
                t += step;
            }
            return 0;
        }
        t += step;
    }
    return 0;
}

__host__ __device__ glm::vec3 getCsg2Normal(glm::vec3 p)
{
    return glm::normalize(glm::vec3(
        csg2SDF(glm::vec3(p.x + EPSILON, p.y, p.z)) - csg2SDF(glm::vec3(p.x - EPSILON, p.y, p.z)),
        csg2SDF(glm::vec3(p.x, p.y + EPSILON, p.z)) - csg2SDF(glm::vec3(p.x, p.y - EPSILON, p.z)),
        csg2SDF(glm::vec3(p.x, p.y, p.z + EPSILON)) - csg2SDF(glm::vec3(p.x, p.y, p.z - EPSILON))
    ));
}

__host__ __device__ float csg2IntersectionTest(Geom surface, Ray r,
    glm::vec3 &intersectionPoint, glm::vec3 &normal, bool &outside)
{
    glm::vec3 pt = multiplyMV(surface.inverseTransform, glm::vec4(r.origin, 1.0f));
    glm::vec3 dir = multiplyMV(surface.inverseTransform, glm::vec4(r.direction, 0.0f));

    // raytrace to get t value
    float t = csg2Raytrace(pt, dir, 100.f, outside);

    // calculate point on surface using ray and t value
    glm::vec3 objPt = pt + t * dir;
    intersectionPoint = multiplyMV(surface.transform, glm::vec4(objPt, 1.0f));

    // calculate normal using gradient
    normal = glm::normalize(multiplyMV(surface.invTranspose, glm::vec4(getCsg2Normal(objPt), 0.0f)));
    if (!outside) normal = -normal;
    return t;
}