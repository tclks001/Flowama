#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

namespace flowama {

inline constexpr float kFixedDeltaSeconds = 1.0F / 120.0F;

struct GranularParticle {
    glm::vec3 position{0.0F};
    glm::vec3 previousPosition{0.0F};
    glm::vec3 velocity{0.0F};
};

struct SimulationDiagnostics {
    std::size_t maximumParticlesPerCell = 0;
    std::size_t candidatePairCount = 0;
    std::size_t overlappingPairCount = 0;
    float maximumPenetration = 0.0F;
};

struct SimulationPerformanceMetrics {
    double predictionMilliseconds = 0.0;
    double wallConstraintMilliseconds = 0.0;
    double gridBuildMilliseconds = 0.0;
    double particleContactMilliseconds = 0.0;
    std::size_t candidatePairCount = 0;
    std::size_t maximumParticlesPerCell = 0;
};

struct SimulationForces {
    glm::vec3 gravityLocal{0.0F};
    glm::vec3 angularVelocityLocal{0.0F};
    glm::vec3 angularAccelerationLocal{0.0F};
};

[[nodiscard]] glm::vec3 RotatingFrameAcceleration(
    const glm::vec3& positionLocal,
    const glm::vec3& velocityLocal,
    const SimulationForces& forces);

class GranularSimulation {
public:
    GranularSimulation();

    void Reset();
    void Step(
        const SimulationForces& forces,
        SimulationPerformanceMetrics* performanceMetrics = nullptr);

    [[nodiscard]] const std::vector<glm::vec3>& ParticlePositions() const;
    [[nodiscard]] const glm::vec3& HalfExtents() const;
    [[nodiscard]] float ParticleRadius() const;
    [[nodiscard]] const SimulationDiagnostics& Diagnostics() const;
    [[nodiscard]] glm::vec3 CenterOfMass() const;
    [[nodiscard]] bool IsValidState() const;

private:
    void InitializeGrid();
    void InitializeParticles();
    void SynchronizeParticlePositions();
    void BuildGrid();
    void SolveWallConstraints();
    void SolveParticleContacts(SimulationPerformanceMetrics* performanceMetrics);
    void ResolvePairContact(
        std::size_t firstParticle,
        std::size_t secondParticle);

    [[nodiscard]] glm::ivec3 GridCoordinates(const glm::vec3& position) const;
    [[nodiscard]] glm::ivec3 GridCoordinatesFromIndex(int cellIndex) const;
    [[nodiscard]] int GridIndex(const glm::ivec3& coordinates) const;
    [[nodiscard]] glm::vec3 FallbackContactNormal(
        std::size_t firstParticle,
        std::size_t secondParticle) const;

    glm::vec3 halfExtents_{1.8F, 0.1F, 3.0F};
    float particleRadius_ = 0.025F;
    float particleFriction_ = 0.70F;
    float wallFriction_ = 0.18F;
    int solverIterations_ = 12;

    glm::ivec3 gridDimensions_{0};
    float gridCellSize_ = 0.0F;
    std::vector<int> cellHeads_;
    std::vector<int> occupiedCells_;
    std::vector<int> particleNext_;

    std::vector<GranularParticle> particles_;
    std::vector<glm::vec3> particlePositions_;
    SimulationDiagnostics diagnostics_;
};

} // namespace flowama
