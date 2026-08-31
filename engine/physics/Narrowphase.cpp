// ==============================================================================
//  DemonEngine::Narrowphase  -  Implementation
// ==============================================================================
#include "Narrowphase.h"
#include "core/Logger.h"
#include <cmath>

namespace Demon {

namespace {

constexpr int kGjkMaxIters = 32;
constexpr int kEpaMaxIters = 64;
constexpr int kMaxVertices = 64;
constexpr int kMaxFaces    = 64;
constexpr int kMaxEdges    = 128;
constexpr float kEpaTolerance = 0.0005f;

glm::vec3 safeNormalize(const glm::vec3& v)
{
    float len2 = glm::dot(v, v);
    if (len2 < 1e-10f) return {1.0f, 0.0f, 0.0f};
    return v * glm::inversesqrt(len2);
}

glm::vec3 orthogonal(const glm::vec3& v)
{
    if (std::fabs(v.x) < std::fabs(v.y))
        return glm::cross(v, glm::vec3(1, 0, 0));
    return glm::cross(v, glm::vec3(0, 1, 0));
}

SupportPoint support(const BoxShape& a, const BoxShape& b, const glm::vec3& dir)
{
    glm::vec3 pA = a.support(dir);
    glm::vec3 pB = b.support(-dir);
    return { pA - pB, pA, pB };
}

struct Simplex {
    std::array<SupportPoint, 4> pts{};
    int size = 0;

    void push(const SupportPoint& p) { pts[size++] = p; }
};

bool handleLine(Simplex& s, glm::vec3& dir)
{
    const glm::vec3& A = s.pts[s.size - 1].point;
    const glm::vec3& B = s.pts[s.size - 2].point;
    glm::vec3 AB = B - A;
    glm::vec3 AO = -A;

    if (glm::dot(AB, AO) > 0.0f) {
        glm::vec3 perp = glm::cross(glm::cross(AB, AO), AB);
        if (glm::dot(perp, perp) < 1e-10f)
            perp = orthogonal(AB);
        dir = safeNormalize(perp);
    } else {
        s.pts[0] = s.pts[s.size - 1];
        s.size = 1;
        dir = safeNormalize(AO);
    }
    return false;
}

bool handleTriangle(Simplex& s, glm::vec3& dir)
{
    const glm::vec3& A = s.pts[2].point;
    const glm::vec3& B = s.pts[1].point;
    const glm::vec3& C = s.pts[0].point;

    glm::vec3 AB = B - A;
    glm::vec3 AC = C - A;
    glm::vec3 AO = -A;

    glm::vec3 ABC = glm::cross(AB, AC);

    glm::vec3 ABperp = glm::cross(ABC, AB);
    if (glm::dot(ABperp, AO) > 0.0f) {
        s.pts[0] = s.pts[1];
        s.pts[1] = s.pts[2];
        s.size = 2;
        glm::vec3 perp = glm::cross(glm::cross(AB, AO), AB);
        if (glm::dot(perp, perp) < 1e-10f)
            perp = orthogonal(AB);
        dir = safeNormalize(perp);
        return false;
    }

    glm::vec3 ACperp = glm::cross(AC, ABC);
    if (glm::dot(ACperp, AO) > 0.0f) {
        s.pts[1] = s.pts[2];
        s.size = 2;
        glm::vec3 perp = glm::cross(glm::cross(AC, AO), AC);
        if (glm::dot(perp, perp) < 1e-10f)
            perp = orthogonal(AC);
        dir = safeNormalize(perp);
        return false;
    }

    if (glm::dot(ABC, AO) > 0.0f) {
        dir = safeNormalize(ABC);
    } else {
        std::swap(s.pts[0], s.pts[1]);
        dir = safeNormalize(-ABC);
    }
    return false;
}

bool handleTetrahedron(Simplex& s, glm::vec3& dir)
{
    const glm::vec3& A = s.pts[3].point;
    const glm::vec3& B = s.pts[2].point;
    const glm::vec3& C = s.pts[1].point;
    const glm::vec3& D = s.pts[0].point;

    glm::vec3 AO = -A;

    glm::vec3 AB = B - A;
    glm::vec3 AC = C - A;
    glm::vec3 AD = D - A;

    glm::vec3 ABC = glm::cross(AB, AC);
    if (glm::dot(ABC, D - A) > 0.0f) ABC = -ABC;
    if (glm::dot(ABC, AO) > 0.0f) {
        s.pts[0] = s.pts[1];
        s.pts[1] = s.pts[2];
        s.pts[2] = s.pts[3];
        s.size = 3;
        dir = safeNormalize(ABC);
        return false;
    }

    glm::vec3 ACD = glm::cross(AC, AD);
    if (glm::dot(ACD, B - A) > 0.0f) ACD = -ACD;
    if (glm::dot(ACD, AO) > 0.0f) {
        SupportPoint Dp = s.pts[0];
        SupportPoint Cp = s.pts[1];
        SupportPoint Ap = s.pts[3];
        s.pts[0] = Dp;
        s.pts[1] = Cp;
        s.pts[2] = Ap;
        s.size = 3;
        dir = safeNormalize(ACD);
        return false;
    }

    glm::vec3 ADB = glm::cross(AD, AB);
    if (glm::dot(ADB, C - A) > 0.0f) ADB = -ADB;
    if (glm::dot(ADB, AO) > 0.0f) {
        SupportPoint Bp = s.pts[2];
        SupportPoint Dp = s.pts[0];
        SupportPoint Ap = s.pts[3];
        s.pts[0] = Bp;
        s.pts[1] = Dp;
        s.pts[2] = Ap;
        s.size = 3;
        dir = safeNormalize(ADB);
        return false;
    }

    return true; // origin inside tetrahedron
}

bool doSimplex(Simplex& s, glm::vec3& dir)
{
    if (s.size == 2) return handleLine(s, dir);
    if (s.size == 3) return handleTriangle(s, dir);
    if (s.size == 4) return handleTetrahedron(s, dir);
    dir = safeNormalize(-s.pts[0].point);
    return false;
}

bool gjk(const BoxShape& a, const BoxShape& b, Simplex& simplex)
{
    glm::vec3 dir = {1, 0, 0};
    simplex.size = 0;
    simplex.push(support(a, b, dir));
    dir = safeNormalize(-simplex.pts[0].point);

    for (int i = 0; i < kGjkMaxIters; ++i) {
        SupportPoint p = support(a, b, dir);
        if (glm::dot(p.point, dir) < 0.0f)
            return false;
        simplex.push(p);
        if (doSimplex(simplex, dir))
            return true;
    }
    return false;
}

void ensureTetrahedron(const BoxShape& a, const BoxShape& b, Simplex& s)
{
    if (s.size >= 4) return;
    if (s.size == 3) {
        const glm::vec3& A = s.pts[2].point;
        const glm::vec3& B = s.pts[1].point;
        const glm::vec3& C = s.pts[0].point;
        glm::vec3 n = glm::cross(B - A, C - A);
        if (glm::dot(n, n) < 1e-10f) n = {1, 0, 0};
        s.pts[3] = support(a, b, n);
        s.size = 4;
        return;
    }
    if (s.size == 2) {
        glm::vec3 ab = s.pts[1].point - s.pts[0].point;
        glm::vec3 n = orthogonal(ab);
        if (glm::dot(n, n) < 1e-10f) n = {1, 0, 0};
        s.pts[2] = support(a, b, n);
        s.size = 3;
        ensureTetrahedron(a, b, s);
        return;
    }
    if (s.size == 1) {
        s.pts[1] = support(a, b, {1, 0, 0});
        s.size = 2;
        ensureTetrahedron(a, b, s);
    }
}

struct Face {
    int a = 0, b = 0, c = 0;
    glm::vec3 normal = {0, 1, 0};
    float distance = 0.0f;
};

struct Edge { int a, b; };

bool addFace(std::array<Face, kMaxFaces>& faces, int& faceCount,
             const std::array<SupportPoint, kMaxVertices>& verts,
             int a, int b, int c)
{
    if (faceCount >= kMaxFaces) return false;
    Face f;
    f.a = a; f.b = b; f.c = c;
    glm::vec3 A = verts[a].point;
    glm::vec3 B = verts[b].point;
    glm::vec3 C = verts[c].point;
    glm::vec3 n = glm::cross(B - A, C - A);
    if (glm::dot(n, n) < 1e-10f) return false;
    n = safeNormalize(n);
    if (glm::dot(n, -A) > 0.0f) {
        std::swap(f.b, f.c);
        n = -n;
    }
    f.normal = n;
    f.distance = glm::dot(n, A);
    faces[faceCount++] = f;
    return true;
}

bool barycentric(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                 const glm::vec3& p, float& u, float& v, float& w)
{
    glm::vec3 v0 = b - a;
    glm::vec3 v1 = c - a;
    glm::vec3 v2 = p - a;
    float d00 = glm::dot(v0, v0);
    float d01 = glm::dot(v0, v1);
    float d11 = glm::dot(v1, v1);
    float d20 = glm::dot(v2, v0);
    float d21 = glm::dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) < 1e-10f) return false;
    v = (d11 * d20 - d01 * d21) / denom;
    w = (d00 * d21 - d01 * d20) / denom;
    u = 1.0f - v - w;
    return true;
}

bool epa(const BoxShape& a, const BoxShape& b, Simplex& s, ContactManifold& out)
{
    ensureTetrahedron(a, b, s);
    if (s.size < 4) return false;

    std::array<SupportPoint, kMaxVertices> verts{};
    int vertCount = s.size;
    for (int i = 0; i < s.size; ++i) verts[i] = s.pts[i];

    std::array<Face, kMaxFaces> faces{};
    int faceCount = 0;

    if (!addFace(faces, faceCount, verts, 0, 1, 2)) return false;
    if (!addFace(faces, faceCount, verts, 0, 3, 1)) return false;
    if (!addFace(faces, faceCount, verts, 0, 2, 3)) return false;
    if (!addFace(faces, faceCount, verts, 1, 3, 2)) return false;

    for (int iter = 0; iter < kEpaMaxIters; ++iter) {
        int closest = -1;
        float minDist = FLT_MAX;
        for (int i = 0; i < faceCount; ++i) {
            if (faces[i].distance < minDist) {
                minDist = faces[i].distance;
                closest = i;
            }
        }
        if (closest == -1) return false;

        Face& face = faces[closest];
        SupportPoint p = support(a, b, face.normal);
        float d = glm::dot(face.normal, p.point);
        if (d - face.distance < kEpaTolerance) {
            glm::vec3 A = verts[face.a].point;
            glm::vec3 B = verts[face.b].point;
            glm::vec3 C = verts[face.c].point;
            glm::vec3 planePoint = face.normal * face.distance;

            float u = 0, v = 0, w = 0;
            if (!barycentric(A, B, C, planePoint, u, v, w)) return false;

            glm::vec3 contactA =
                verts[face.a].pointA * u +
                verts[face.b].pointA * v +
                verts[face.c].pointA * w;

            glm::vec3 contactB =
                verts[face.a].pointB * u +
                verts[face.b].pointB * v +
                verts[face.c].pointB * w;

            glm::vec3 n = face.normal;
            if (glm::dot(n, contactA - contactB) < 0.0f)
                n = -n;

            out.normal = safeNormalize(n);
            out.penetration = std::fabs(face.distance);
            out.pointCount = 1;
            out.points[0].position = (contactA + contactB) * 0.5f;
            out.points[0].penetration = out.penetration;
            return true;
        }

        if (vertCount >= kMaxVertices) return false;
        int newIndex = vertCount++;
        verts[newIndex] = p;

        std::array<Edge, kMaxEdges> edges{};
        int edgeCount = 0;

        for (int i = 0; i < faceCount; ) {
            Face& f = faces[i];
            glm::vec3 A = verts[f.a].point;
            if (glm::dot(f.normal, p.point - A) > 0.0f) {
                auto addEdge = [&](int a, int b) {
                    for (int e = 0; e < edgeCount; ++e) {
                        if (edges[e].a == b && edges[e].b == a) {
                            edges[e] = edges[--edgeCount];
                            return;
                        }
                    }
                    if (edgeCount < kMaxEdges)
                        edges[edgeCount++] = {a, b};
                };

                addEdge(f.a, f.b);
                addEdge(f.b, f.c);
                addEdge(f.c, f.a);

                faces[i] = faces[--faceCount];
                continue;
            }
            ++i;
        }

        for (int e = 0; e < edgeCount; ++e) {
            if (!addFace(faces, faceCount, verts, edges[e].a, edges[e].b, newIndex)) {
                return false;
            }
        }
    }

    return false;
}

} // namespace

glm::vec3 BoxShape::support(const glm::vec3& dir) const
{
    glm::vec3 localDir = glm::transpose(orientation) * dir;
    glm::vec3 s = {
        (localDir.x >= 0.0f) ? halfExtents.x : -halfExtents.x,
        (localDir.y >= 0.0f) ? halfExtents.y : -halfExtents.y,
        (localDir.z >= 0.0f) ? halfExtents.z : -halfExtents.z
    };
    return center + orientation * s;
}

bool Narrowphase::computeManifold(const BoxShape& a, const BoxShape& b, ContactManifold& out)
{
    Simplex simplex;
    if (!gjk(a, b, simplex))
        return false;

    if (!epa(a, b, simplex, out)) {
        DEMON_LOG_WARN("Narrowphase: EPA failed to converge");
        return false;
    }
    return true;
}

} // namespace Demon
