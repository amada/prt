#pragma once

#include "prt.h"
#include "stats.h"
#include "vecmath.h"

namespace prt
{

// TODO
// template<typename T>
//struct RayT

class Ray
{
public:
    Ray() = default;
    Vector3f org;
    Vector3f dir;
    float maxT;
    Vector3f invDir;

    SoaMask swapXZ;
    SoaMask swapYZ;

    void prepare() {
        invDir = 1.0f/dir;

        swapXZ.setAll(false);
        swapYZ.setAll(false);

        // Set the largest magnitute element to z-axis
//        auto v = abs(dir);
        auto e = dir.GetLongestElement();
        if (e == 0) {
            swapXZ.setAll(true);
        } else if (e == 1) {
            swapYZ.setAll(true);
        }
    }
};

class SoaRay
{
public:
    SoaRay() = default;
    SoaVector3f org;
    SoaVector3f dir;
    SoaVector3f invDir;
    SoaFloat maxT;

    SoaMask swapXZ;
    SoaMask swapYZ;

//    bool m_verbose = false;

// TODO: should calculate after org, dir are set
    void prepare() {
        invDir = 1.0f/dir;
        auto d = dir;
        // Pre-calculation
        // Find an element having the largest magnitude
        auto abs_d = d.abs();

        auto max_e = abs_d.maximumElement();
        auto mask_x = max_e.equals(abs_d.getX()); // Mask if max element is x
        auto mask_y = max_e.equals(abs_d.getY()).computeAnd(mask_x.computeNot()); // Mask if max element is y

        swapXZ = mask_x;
        swapYZ = mask_y;
    }
};

struct SingleRayPacket
{
    Ray ray;

    SingleRayPacket() = default;
    SingleRayPacket(const Ray& ray) : ray(ray) {}
};

struct RayPacket
{
    static const uint32_t kSize = 8;
    static const uint32_t kVectorCount = kSize/SoaConstants::kLaneCount;
    SoaRay rays[kVectorCount];
    Vector3f avgDir;

    RayPacket() = default;
    RayPacket(const Vector3f* org, const Vector3f* dir) {
        for (uint32_t i = 0; i < kVectorCount; i++) {
            auto& ray = rays[i];
            ray.org = SoaVector3f(&org[i*SoaConstants::kLaneCount]);
            ray.dir = SoaVector3f(&dir[i*SoaConstants::kLaneCount]);
        }
    }
};


struct RayPacketMask
{
    SoaMask masks[RayPacket::kVectorCount];

#if 0
    static RayPacketMask initAllTrue() {
        RayPacketMask result;
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            result.masks[i] = SoaMask::initAllTrue();
        }
        return result;
    }
#endif
    RayPacketMask() = default;
    RayPacketMask(uint32_t mask) {
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            masks[i] = SoaMask(mask & ((1 << SoaConstants::kLaneCount) - 1));
            mask >>= SoaConstants::kLaneCount;
        }
    }

    RayPacketMask operator&&(const RayPacketMask& other) const {
        RayPacketMask result;
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            result.masks[i] = masks[i] && other.masks[i];
        }
        return result;
    }

    RayPacketMask operator||(const RayPacketMask& other) const {
        RayPacketMask result;
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            result.masks[i] = masks[i] || other.masks[i];
        }
        return result;
    }

    void computeAndSelf(uint32_t v, const SoaMask& mask) {
        masks[v] = masks[v] && mask;
    }

    void computeOrSelf(uint32_t v, const SoaMask& mask) {
        masks[v] = masks[v] || mask;
    }

    RayPacketMask computeNot() const {
        RayPacketMask result;
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            result.masks[i] = masks[i].computeNot();
        }
        return result;
    }

    void setAll(bool b) {
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            masks[i].setAll(b);
        }
    }

    bool anyTrue(uint32_t v) const {
        return masks[v].anyTrue();
    }

    bool anyTrue() const {
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            if (masks[i].anyTrue()) {
                return true;
            }
        }
        return false;
    }

    bool allTrue() const {
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            if (!masks[i].allTrue()) {
                return false;
            }
        }
        return true;
    }
};

template<typename T, typename U>
struct RayHitT
{
    static const constexpr float kMissT = -1.0f;

    RayHitT() = default;
    bool isHit() const { return t != kMissT; }

    T t;
    T i, j, k; // barycentric coordinate

    U primId;
    U meshId;
};

using RayHit = RayHitT<float, uint32_t>;
using SoaRayHit = RayHitT<SoaFloat, SoaInt>;

struct SingleRayHitPacket
{
    RayHit hit;

#ifdef PRT_ENABLE_STATS
    Stats stats;
#endif

    void setMaxT(const SingleRayPacket& packet) {
        hit.t = packet.ray.maxT;
    }

    void setMissForMaxT(const SingleRayPacket& packet) {
        if (hit.t == packet.ray.maxT)
            hit.t = RayHit::kMissT;        
    }
};

struct RayHitPacket
{
    SoaRayHit hits[RayPacket::kVectorCount];

#ifdef PRT_ENABLE_STATS
    Stats stats;
#endif

    RayPacketMask generateMask() const {
        RayPacketMask mask;
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++)
            mask.masks[i] = hits[i].t.notEquals(RayHit::kMissT);
        return mask;
    }

    void setMaxT(const RayPacket& packet) {
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++)
            hits[i].t = packet.rays[i].maxT;
    }

    void setMissForMaxT(const RayPacket& packet) {
        for (uint32_t i = 0; i < RayPacket::kVectorCount; i++) {
            auto mask = hits[i].t.equals(packet.rays[i].maxT);
            hits[i].t = select(hits[i].t, RayHit::kMissT, mask);
        }
    }        
};

struct SingleRayOccludedPacket
{
    bool occluded;

#ifdef PRT_ENABLE_STATS
    Stats stats;
#endif
};


} // namespace prt