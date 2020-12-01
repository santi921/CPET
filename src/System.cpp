/*
 * C++ STL HEADER FILES
 */
#include <thread>
#include <cmath>

/*
 * EXTERNAL LIBRARY HEADER FILES
 */
#include "spdlog/sinks/stdout_sinks.h"
#include "Guarded.h"

/*
 * CPET HEADER FILES
 */
#include "System.h"

#define PERM_SPACE 0.0055263495

System::System(const std::string_view &proteinFile, const std::string_view &optionsFile)
    : _pointCharges(), _center(), _basisMatrix(Eigen::Matrix3d::Identity()), _region(nullptr), _numberOfSamples(0) {

    _loadOptions(optionsFile);
    if (_region == nullptr) {
        throw std::exception();
    }

    _loadPDB(proteinFile);
    if (_pointCharges.empty()) {
        throw std::exception();
    }

    _translateToCenter();
    _toUserBasis();
}

Eigen::Vector3d System::electricField(const Eigen::Vector3d &position) const noexcept(true) {
    Eigen::Vector3d result(0, 0, 0);

    for (const auto &pc : _pointCharges) {
        Eigen::Vector3d d = (position - pc.coordinate);
        double dNorm = d.norm();
        result += ((pc.charge * d) / (dNorm * dNorm * dNorm));
    }
    result /= (1.0 / (4.0 * M_PI * PERM_SPACE));
    return result;
}

std::vector<PathSample> System::calculateTopology(const size_t &procs) {
    SPDLOG_INFO("======[Sampling topology]======");
    SPDLOG_INFO("[Npoints] ==>> {}", _numberOfSamples);
    SPDLOG_INFO("[Threads] ==>> {}", procs);

    std::vector<PathSample> sampleResults(_numberOfSamples);

    std::shared_ptr<spdlog::logger> thread_logger;
    if (!(thread_logger = spdlog::get("Thread"))) {
        thread_logger = spdlog::stdout_logger_mt("Thread");
    }

    if (procs == 1) {
        size_t samples = _numberOfSamples;
        while(samples-- > 0){
            sampleResults.push_back(_sample());
        }
    }
    else {
#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_DEBUG
        thread_logger->set_pattern("[Thread: %t] [%l] %v");
#else
        thread_logger->set_pattern("[Thread: %t] ==>> %v");
#endif

        std::atomic_int samples = _numberOfSamples;
        libguarded::plain_guarded<std::vector<PathSample>> shared_vector(_numberOfSamples);

        SPDLOG_INFO("====[Initializing threads]====");
        std::vector<std::thread> workers;
        workers.reserve(procs);

        for(size_t i = 0; i < procs; i++){
            workers.emplace_back(std::thread([&samples, &shared_vector, this](){
//                auto thread_logger = spdlog::get("Thread");
//                thread_logger->info("Starting");
                while(samples-- > 0){
                    auto s = _sample();
                    auto vector_handler = shared_vector.lock();
                    vector_handler->push_back(s);
                }
            }
            ));
        }

        for (auto &thread : workers) {
            thread.join();
        }
        {
            auto vector_handle = shared_vector.lock();
            sampleResults = *(vector_handle);
        }
    }
    return sampleResults;
}

void System::_loadPDB(const std::string_view& name) {
    std::uintmax_t fileSize = std::filesystem::file_size(name);
    _pointCharges.reserve(fileSize / 69);

    forEachLineIn(name, [this](const std::string &line) {
        if (line.substr(0, 4) == "ATOM" || line.substr(0, 6) == "HETATM") {
            this->_pointCharges.emplace_back(Eigen::Vector3d({std::stod(line.substr(31, 8)),
                                                              std::stod(line.substr(39, 8)),
                                                              std::stod(line.substr(47, 8))}),
                                             std::stod(line.substr(55, 8))
            );
        }
    });

    SPDLOG_INFO("Loaded in {} point charges from file {}", _pointCharges.size(), name);
}

void System::_loadOptions(const std::string_view &optionsFile) {
    // TODO somehow place some checks here...
    forEachLineIn(optionsFile, [this](const std::string &line) {
        if (line.substr(0, 6) == "center") {
            std::vector<std::string> info = split(line.substr(6), ' ');
            this->_center = {stod(info[0]), stod(info[1]), stod(info[2])};
        }
        else if (line.substr(0, 2) == "v1") {
            std::vector<std::string> info = split(line.substr(2), ' ');
            Eigen::Vector3d v1(stod(info[0]), stod(info[1]), stod(info[2]));
            this->_basisMatrix.block(0, 0, 3, 1) = v1;
        }
        else if (line.substr(0, 2) == "v2") {
            std::vector<std::string> info = split(line.substr(2), ' ');
            Eigen::Vector3d v2(stod(info[0]), stod(info[1]), stod(info[2]));
            this->_basisMatrix.block(0, 1, 3, 1) = v2;
        }
        else if (line.substr(0, 6) == "volume") {
            std::vector<std::string> info = split(line.substr(6), ' ');
            if (info[0] == "box") {
                std::array<double, 3> dims = {std::stod(info[1]), std::stod(info[2]), std::stod(info[3])};
                this->_region = std::make_unique<Box>(dims);
            }
        }
        else if (line.substr(0, 6) == "sample") {
            std::vector<std::string> info = split(line.substr(6), ' ');
            this->_numberOfSamples = static_cast<size_t>(std::stoi(info[0]));
        }
    });

    _basisMatrix.block(0, 2, 3, 1) = _basisMatrix.col(0).cross(_basisMatrix.col(1));
    
    // TODO make this a seperate function 
    SPDLOG_INFO("=====[Options | {}]=====", optionsFile);
    //TODO make a function overload for these...
//    SPDLOG_INFO("[V1]     ==>> [ {} ]", _basisMatrix.block(0, 0, 3, 1).transpose());
//    SPDLOG_INFO("[V2]     ==>> [ {} ]", _basisMatrix.block(0, 1, 3, 1).transpose());
//    SPDLOG_INFO("[V3]     ==>> [ {} ]", _basisMatrix.block(0, 2, 3, 1).transpose());
//    SPDLOG_INFO("[Center] ==>> [ {} ]", _center.transpose());
//    SPDLOG_INFO("[Region] ==>> {}", _region->description());
}

PathSample System::_sample() noexcept(true) {
    Eigen::Vector3d initialPosition;
    const int maxSteps = _randomDistance();

    // TODO can we come up with something with either guarded or not...
    // The issue is that the distributions and gen will both be modified...
    {
        std::unique_lock<std::mutex> lock(_mutex_volume);
        initialPosition = _region->randomPoint();
    }

    Eigen::Vector3d finalPosition = initialPosition;

    int steps = 0;

    while (_region->isInside(finalPosition) && ++steps < maxSteps) {
        finalPosition = _next(finalPosition);
    }

    // TODO theres a better way of doing this; I can return the result and then push back onto some stack or deque...
    return {(finalPosition - initialPosition).norm(),
                 (_curvature(finalPosition) + _curvature(initialPosition)) / 2.0};

}

double System::_curvature(const Eigen::Vector3d &alpha_0) const noexcept(true) {
    Eigen::Vector3d alpha_prime = electricField(alpha_0);
    Eigen::Vector3d alpha_1 = _next(alpha_0);

    // Measures how much "time" we spent going forward
    // delta alpha/delta t = E, in the limit of delta t -> 0
    // then we have delta alpha/E = delta t
    double delta_t = (alpha_1 - alpha_0).norm() / alpha_prime.norm();

    // Simple directional derivative of the electric field in that direction
    Eigen::Vector3d alpha_prime_prime = (electricField(alpha_1) - alpha_prime) / delta_t;

    double alpha_prime_norm = alpha_prime.norm();

    return (alpha_prime.cross(alpha_prime_prime)).norm() / (alpha_prime_norm * alpha_prime_norm * alpha_prime_norm);

}

