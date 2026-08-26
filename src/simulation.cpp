#include "simulation.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace flowama {
namespace {

constexpr float kContactEpsilon = 0.000001F;

float DeterministicUnitValue(std::uint32_t value)
{
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00ffffffU) / static_cast<float>(0x00ffffffU);
}

} // namespace

GranularSimulation::GranularSimulation()
{
    InitializeGrid();
    Reset();
}

glm::vec3 RotatingFrameAcceleration(
    const glm::vec3& positionLocal,
    const glm::vec3& velocityLocal,
    const SimulationForces& forces)
{
    const glm::vec3& angularVelocity = forces.angularVelocityLocal;
    return forces.gravityLocal
        - 2.0F * glm::cross(angularVelocity, velocityLocal)
        - glm::cross(forces.angularAccelerationLocal, positionLocal)
        - glm::cross(angularVelocity, glm::cross(angularVelocity, positionLocal));
}

void GranularSimulation::Reset()
{
    InitializeParticles();
    particleNext_.assign(particles_.size(), -1);
    diagnostics_ = {};
    SynchronizeParticlePositions();
}

void GranularSimulation::Step(
    const SimulationForces& forces,
    SimulationPerformanceMetrics* const performanceMetrics)
{
    diagnostics_ = {};
    if (performanceMetrics != nullptr) {
        *performanceMetrics = {};
    }

    const auto predictionStart = performanceMetrics != nullptr
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    for (GranularParticle& particle : particles_) {
        particle.previousPosition = particle.position;
        particle.velocity += RotatingFrameAcceleration(
            particle.position,
            particle.velocity,
            forces) * kFixedDeltaSeconds;
        particle.position += particle.velocity * kFixedDeltaSeconds;
    }
    if (performanceMetrics != nullptr) {
        performanceMetrics->predictionMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - predictionStart).count();
    }

    for (int iteration = 0; iteration < solverIterations_; ++iteration) {
        const auto firstWallStart = performanceMetrics != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        SolveWallConstraints();
        if (performanceMetrics != nullptr) {
            performanceMetrics->wallConstraintMilliseconds += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - firstWallStart).count();
        }

        SolveParticleContacts(performanceMetrics);

        const auto secondWallStart = performanceMetrics != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        SolveWallConstraints();
        if (performanceMetrics != nullptr) {
            performanceMetrics->wallConstraintMilliseconds += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - secondWallStart).count();
        }
    }

    for (GranularParticle& particle : particles_) {
        particle.velocity = (particle.position - particle.previousPosition) / kFixedDeltaSeconds;
    }
    SynchronizeParticlePositions();
}

const std::vector<glm::vec3>& GranularSimulation::ParticlePositions() const
{
    return particlePositions_;
}

const glm::vec3& GranularSimulation::HalfExtents() const
{
    return halfExtents_;
}

float GranularSimulation::ParticleRadius() const
{
    return particleRadius_;
}

const SimulationDiagnostics& GranularSimulation::Diagnostics() const
{
    return diagnostics_;
}

glm::vec3 GranularSimulation::CenterOfMass() const
{
    glm::vec3 center{0.0F};
    for (const GranularParticle& particle : particles_) {
        center += particle.position;
    }
    return particles_.empty() ? center : center / static_cast<float>(particles_.size());
}

bool GranularSimulation::IsValidState() const
{
    const glm::vec3 allowedExtents = halfExtents_ - glm::vec3{particleRadius_};
    for (const GranularParticle& particle : particles_) {
        if (!std::isfinite(particle.position.x)
            || !std::isfinite(particle.position.y)
            || !std::isfinite(particle.position.z)
            || !std::isfinite(particle.velocity.x)
            || !std::isfinite(particle.velocity.y)
            || !std::isfinite(particle.velocity.z)
            || glm::any(glm::greaterThan(glm::abs(particle.position), allowedExtents + 0.0001F))) {
            return false;
        }
    }
    return true;
}

void GranularSimulation::InitializeGrid()
{
    gridCellSize_ = particleRadius_ * 2.0F;
    const glm::vec3 fullExtents = halfExtents_ * 2.0F;
    gridDimensions_ = glm::ivec3{
        static_cast<int>(std::ceil(fullExtents.x / gridCellSize_)),
        static_cast<int>(std::ceil(fullExtents.y / gridCellSize_)),
        static_cast<int>(std::ceil(fullExtents.z / gridCellSize_))};
    const int cellCount = gridDimensions_.x * gridDimensions_.y * gridDimensions_.z;
    cellHeads_.assign(static_cast<std::size_t>(cellCount), -1);
}

void GranularSimulation::InitializeParticles()
{
    constexpr int columns = 16;
    constexpr int depthLayers = 2;
    constexpr int heightLayers = 8;
    constexpr float spacing = 0.055F;
    constexpr float jitterAmplitude = 0.001F;
    static_assert(columns * depthLayers * heightLayers == 256);

    particles_.clear();
    particles_.reserve(columns * depthLayers * heightLayers);
    const glm::vec3 origin{
        -0.5F * static_cast<float>(columns - 1) * spacing,
        -0.5F * static_cast<float>(depthLayers - 1) * spacing,
        1.15F};

    std::uint32_t particleIndex = 0;
    for (int height = 0; height < heightLayers; ++height) {
        for (int depth = 0; depth < depthLayers; ++depth) {
            for (int column = 0; column < columns; ++column) {
                const glm::vec3 jitter{
                    (DeterministicUnitValue(particleIndex * 3U + 0U) - 0.5F) * 2.0F * jitterAmplitude,
                    (DeterministicUnitValue(particleIndex * 3U + 1U) - 0.5F) * 2.0F * jitterAmplitude,
                    (DeterministicUnitValue(particleIndex * 3U + 2U) - 0.5F) * 2.0F * jitterAmplitude};
                GranularParticle particle;
                particle.position = origin + glm::vec3{
                    static_cast<float>(column) * spacing,
                    static_cast<float>(depth) * spacing,
                    static_cast<float>(height) * spacing}
                    + jitter;
                particle.previousPosition = particle.position;
                particles_.push_back(particle);
                ++particleIndex;
            }
        }
    }
}

void GranularSimulation::SynchronizeParticlePositions()
{
    particlePositions_.resize(particles_.size());
    for (std::size_t index = 0; index < particles_.size(); ++index) {
        particlePositions_[index] = particles_[index].position;
    }
}

void GranularSimulation::BuildGrid()
{
    for (const int occupiedCell : occupiedCells_) {
        cellHeads_[static_cast<std::size_t>(occupiedCell)] = -1;
    }
    occupiedCells_.clear();
    diagnostics_.maximumParticlesPerCell = 0;

    for (std::size_t particleIndex = 0; particleIndex < particles_.size(); ++particleIndex) {
        const int cellIndex = GridIndex(GridCoordinates(particles_[particleIndex].position));
        int& head = cellHeads_[static_cast<std::size_t>(cellIndex)];
        if (head == -1) {
            occupiedCells_.push_back(cellIndex);
        }
        particleNext_[particleIndex] = head;
        head = static_cast<int>(particleIndex);
    }

    std::sort(occupiedCells_.begin(), occupiedCells_.end());

    for (const int occupiedCell : occupiedCells_) {
        std::size_t occupancy = 0;
        for (int particleIndex = cellHeads_[static_cast<std::size_t>(occupiedCell)];
             particleIndex != -1;
             particleIndex = particleNext_[static_cast<std::size_t>(particleIndex)]) {
            ++occupancy;
        }
        diagnostics_.maximumParticlesPerCell = std::max(
            diagnostics_.maximumParticlesPerCell,
            occupancy);
    }
}

void GranularSimulation::SolveWallConstraints()
{
    const glm::vec3 allowedExtents = halfExtents_ - glm::vec3{particleRadius_};
    for (GranularParticle& particle : particles_) {
        for (int axis = 0; axis < 3; ++axis) {
            const float unclampedPosition = particle.position[axis];
            particle.position[axis] = glm::clamp(
                particle.position[axis],
                -allowedExtents[axis],
                allowedExtents[axis]);
            const float normalCorrection = std::abs(particle.position[axis] - unclampedPosition);
            if (normalCorrection <= 0.0F) {
                continue;
            }

            glm::vec3 tangentialDisplacement = particle.position - particle.previousPosition;
            tangentialDisplacement[axis] = 0.0F;
            const float tangentialLength = glm::length(tangentialDisplacement);
            if (tangentialLength <= kContactEpsilon) {
                continue;
            }

            const float frictionCorrection = std::min(
                tangentialLength,
                wallFriction_ * normalCorrection);
            particle.position -= tangentialDisplacement
                * (frictionCorrection / tangentialLength);
        }
    }
}

void GranularSimulation::SolveParticleContacts(
    SimulationPerformanceMetrics* const performanceMetrics)
{
    const auto gridBuildStart = performanceMetrics != nullptr
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    BuildGrid();
    if (performanceMetrics != nullptr) {
        performanceMetrics->gridBuildMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - gridBuildStart).count();
    }

    diagnostics_.candidatePairCount = 0;
    diagnostics_.overlappingPairCount = 0;
    diagnostics_.maximumPenetration = 0.0F;

    const auto contactStart = performanceMetrics != nullptr
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    for (const int homeCell : occupiedCells_) {
        const glm::ivec3 homeCoordinates = GridCoordinatesFromIndex(homeCell);
        const int homeHead = cellHeads_[static_cast<std::size_t>(homeCell)];
        for (int neighborZ = std::max(0, homeCoordinates.z - 1);
             neighborZ <= std::min(gridDimensions_.z - 1, homeCoordinates.z + 1);
             ++neighborZ) {
            for (int neighborY = std::max(0, homeCoordinates.y - 1);
                 neighborY <= std::min(gridDimensions_.y - 1, homeCoordinates.y + 1);
                 ++neighborY) {
                for (int neighborX = std::max(0, homeCoordinates.x - 1);
                     neighborX <= std::min(gridDimensions_.x - 1, homeCoordinates.x + 1);
                     ++neighborX) {
                    const int neighborCell = GridIndex({neighborX, neighborY, neighborZ});
                    if (neighborCell < homeCell
                        || cellHeads_[static_cast<std::size_t>(neighborCell)] == -1) {
                        continue;
                    }

                    if (neighborCell == homeCell) {
                        for (int first = homeHead;
                             first != -1;
                             first = particleNext_[static_cast<std::size_t>(first)]) {
                            for (int second = particleNext_[static_cast<std::size_t>(first)];
                                 second != -1;
                                 second = particleNext_[static_cast<std::size_t>(second)]) {
                                ResolvePairContact(
                                    static_cast<std::size_t>(first),
                                    static_cast<std::size_t>(second));
                            }
                        }
                    } else {
                        for (int first = homeHead;
                             first != -1;
                             first = particleNext_[static_cast<std::size_t>(first)]) {
                            for (int second = cellHeads_[static_cast<std::size_t>(neighborCell)];
                                 second != -1;
                                 second = particleNext_[static_cast<std::size_t>(second)]) {
                                ResolvePairContact(
                                    static_cast<std::size_t>(first),
                                    static_cast<std::size_t>(second));
                            }
                        }
                    }
                }
            }
        }
    }

    if (performanceMetrics != nullptr) {
        performanceMetrics->particleContactMilliseconds += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - contactStart).count();
        performanceMetrics->candidatePairCount += diagnostics_.candidatePairCount;
        performanceMetrics->maximumParticlesPerCell = std::max(
            performanceMetrics->maximumParticlesPerCell,
            diagnostics_.maximumParticlesPerCell);
    }
}

void GranularSimulation::ResolvePairContact(
    const std::size_t firstParticle,
    const std::size_t secondParticle)
{
    ++diagnostics_.candidatePairCount;
    const glm::vec3 separation = particles_[firstParticle].position
        - particles_[secondParticle].position;
    const float distanceSquared = glm::dot(separation, separation);
    const float minimumDistance = particleRadius_ * 2.0F;
    if (distanceSquared >= minimumDistance * minimumDistance) {
        return;
    }

    const float distance = std::sqrt(std::max(distanceSquared, 0.0F));
    const glm::vec3 normal = distance > kContactEpsilon
        ? separation / distance
        : FallbackContactNormal(firstParticle, secondParticle);
    const float penetration = minimumDistance - distance;
    ++diagnostics_.overlappingPairCount;
    diagnostics_.maximumPenetration = std::max(diagnostics_.maximumPenetration, penetration);

    glm::vec3 relativeCorrection = penetration * normal;
    const glm::vec3 relativeDisplacement =
        (particles_[firstParticle].position - particles_[firstParticle].previousPosition)
        - (particles_[secondParticle].position - particles_[secondParticle].previousPosition);
    const glm::vec3 tangentialDisplacement = relativeDisplacement
        - glm::dot(relativeDisplacement, normal) * normal;
    const float tangentialLength = glm::length(tangentialDisplacement);
    if (tangentialLength > kContactEpsilon) {
        const float frictionCorrection = std::min(
            tangentialLength,
            particleFriction_ * penetration);
        relativeCorrection -= tangentialDisplacement
            * (frictionCorrection / tangentialLength);
    }

    particles_[firstParticle].position += 0.5F * relativeCorrection;
    particles_[secondParticle].position -= 0.5F * relativeCorrection;
}

glm::ivec3 GranularSimulation::GridCoordinates(const glm::vec3& position) const
{
    const glm::vec3 coordinates = glm::floor((position + halfExtents_) / gridCellSize_);
    return glm::clamp(
        glm::ivec3{coordinates},
        glm::ivec3{0},
        gridDimensions_ - glm::ivec3{1});
}

glm::ivec3 GranularSimulation::GridCoordinatesFromIndex(const int cellIndex) const
{
    const int x = cellIndex % gridDimensions_.x;
    const int remaining = cellIndex / gridDimensions_.x;
    const int y = remaining % gridDimensions_.y;
    const int z = remaining / gridDimensions_.y;
    return {x, y, z};
}

int GranularSimulation::GridIndex(const glm::ivec3& coordinates) const
{
    return coordinates.x + gridDimensions_.x * (coordinates.y + gridDimensions_.y * coordinates.z);
}

glm::vec3 GranularSimulation::FallbackContactNormal(
    const std::size_t firstParticle,
    const std::size_t secondParticle) const
{
    const std::uint32_t pairHash = static_cast<std::uint32_t>(firstParticle * 73856093U)
        ^ static_cast<std::uint32_t>(secondParticle * 19349663U);
    constexpr std::array<glm::vec3, 6> directions{{
        {1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, -1.0F},
    }};
    return directions[pairHash % directions.size()];
}

} // namespace flowama
