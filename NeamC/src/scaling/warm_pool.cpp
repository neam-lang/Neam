// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Warm Pool Manager Implementation

#include "neamc/scaling/warm_pool.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace neamc::scaling {

struct WarmPoolManager::Impl {
    WarmPoolConfig config;
    std::unordered_map<std::string, WarmInstance> instances;
    HealthCheckCallback health_check_callback;
    Stats stats{};
    std::atomic<bool> running{false};
    std::thread background_thread;
    std::mutex mutex;
    std::condition_variable cv;

    Impl(WarmPoolConfig cfg) : config(std::move(cfg)) {}

    ~Impl() {
        stop();
    }

    void stop() {
        running = false;
        cv.notify_all();
        if (background_thread.joinable()) {
            background_thread.join();
        }
    }

    std::string generate_instance_id() {
        static std::atomic<int> counter{0};
        return "warm-" + std::to_string(++counter);
    }

    void background_loop() {
        while (running) {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait_for(lock, config.health_check_interval, [this] {
                return !running.load();
            });

            if (!running) break;

            // Health check
            if (health_check_callback) {
                for (auto& [id, instance] : instances) {
                    if (instance.state == WarmInstance::State::Ready) {
                        if (!health_check_callback(instance)) {
                            instance.state = WarmInstance::State::Terminated;
                            stats.health_check_failures++;
                        }
                    }
                }
            }

            // Clean up terminated instances
            for (auto it = instances.begin(); it != instances.end();) {
                if (it->second.state == WarmInstance::State::Terminated) {
                    it = instances.erase(it);
                } else {
                    ++it;
                }
            }

            // Auto-replenish if needed
            if (config.auto_replenish) {
                int ready_count = 0;
                for (const auto& [id, instance] : instances) {
                    if (instance.state == WarmInstance::State::Ready) {
                        ready_count++;
                    }
                }

                while (ready_count < config.min_warm_instances &&
                       static_cast<int>(instances.size()) < config.max_warm_instances) {
                    // Create a new warm instance
                    WarmInstance instance;
                    instance.instance_id = generate_instance_id();
                    instance.state = WarmInstance::State::Ready;
                    instance.warmed_at = std::chrono::system_clock::now();
                    instance.last_used = instance.warmed_at;
                    instance.cpu_cores = 2;
                    instance.memory_mb = 4096;
                    instances[instance.instance_id] = instance;
                    stats.total_instances_created++;
                    ready_count++;
                }
            }
        }
    }
};

WarmPoolManager::WarmPoolManager(WarmPoolConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

WarmPoolManager::~WarmPoolManager() = default;

WarmPoolManager::WarmPoolManager(WarmPoolManager&&) noexcept = default;
WarmPoolManager& WarmPoolManager::operator=(WarmPoolManager&&) noexcept = default;

void WarmPoolManager::ensure_warm_instances(int count) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    int current_ready = 0;
    for (const auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready) {
            current_ready++;
        }
    }

    while (current_ready < count &&
           static_cast<int>(impl_->instances.size()) < impl_->config.max_warm_instances) {
        WarmInstance instance;
        instance.instance_id = impl_->generate_instance_id();
        instance.state = WarmInstance::State::Ready;
        instance.warmed_at = std::chrono::system_clock::now();
        instance.last_used = instance.warmed_at;
        instance.cpu_cores = 2;
        instance.memory_mb = 4096;
        impl_->instances[instance.instance_id] = instance;
        impl_->stats.total_instances_created++;
        current_ready++;
    }
}

std::optional<WarmInstance> WarmPoolManager::acquire_instance() {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    for (auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready) {
            instance.state = WarmInstance::State::InUse;
            instance.last_used = std::chrono::system_clock::now();
            impl_->stats.total_instances_acquired++;
            impl_->stats.current_in_use_count++;
            return instance;
        }
    }

    return std::nullopt;
}

std::optional<WarmInstance> WarmPoolManager::acquire_instance(
    const std::string& instance_type,
    const std::string& region
) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    for (auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready) {
            if (!instance_type.empty() && instance.instance_type != instance_type) {
                continue;
            }
            if (!region.empty() && instance.region != region) {
                continue;
            }
            instance.state = WarmInstance::State::InUse;
            instance.last_used = std::chrono::system_clock::now();
            impl_->stats.total_instances_acquired++;
            impl_->stats.current_in_use_count++;
            return instance;
        }
    }

    return std::nullopt;
}

void WarmPoolManager::release_instance(const std::string& instance_id, bool keep_warm) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    auto it = impl_->instances.find(instance_id);
    if (it != impl_->instances.end()) {
        if (keep_warm) {
            it->second.state = WarmInstance::State::Ready;
            it->second.last_used = std::chrono::system_clock::now();
            impl_->stats.total_instances_released++;
        } else {
            it->second.state = WarmInstance::State::Terminated;
            impl_->stats.total_instances_terminated++;
        }
        impl_->stats.current_in_use_count--;
    }
}

void WarmPoolManager::prewarm_with_agent(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    for (auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready) {
            if (std::find(instance.preloaded_agents.begin(),
                         instance.preloaded_agents.end(),
                         agent_id) == instance.preloaded_agents.end()) {
                instance.preloaded_agents.push_back(agent_id);
            }
        }
    }
}

void WarmPoolManager::prewarm_with_agents(const std::vector<std::string>& agent_ids) {
    for (const auto& agent_id : agent_ids) {
        prewarm_with_agent(agent_id);
    }
}

int WarmPoolManager::warm_instance_count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return static_cast<int>(impl_->instances.size());
}

int WarmPoolManager::available_instance_count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    int count = 0;
    for (const auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready) {
            count++;
        }
    }
    return count;
}

int WarmPoolManager::in_use_instance_count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    int count = 0;
    for (const auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::InUse) {
            count++;
        }
    }
    return count;
}

double WarmPoolManager::current_warm_pool_cost_per_hour() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    double total = 0;
    for (const auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready ||
            instance.state == WarmInstance::State::InUse) {
            total += instance.hourly_cost;
        }
    }
    return total;
}

std::vector<WarmInstance> WarmPoolManager::list_instances() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<WarmInstance> result;
    for (const auto& [id, instance] : impl_->instances) {
        result.push_back(instance);
    }
    return result;
}

std::vector<WarmInstance> WarmPoolManager::list_available_instances() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<WarmInstance> result;
    for (const auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready) {
            result.push_back(instance);
        }
    }
    return result;
}

void WarmPoolManager::set_health_check_callback(HealthCheckCallback callback) {
    impl_->health_check_callback = std::move(callback);
}

void WarmPoolManager::run_health_check() {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    if (!impl_->health_check_callback) return;

    for (auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready) {
            if (!impl_->health_check_callback(instance)) {
                instance.state = WarmInstance::State::Terminated;
                impl_->stats.health_check_failures++;
            }
        }
    }
}

void WarmPoolManager::set_config(WarmPoolConfig config) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->config = std::move(config);
}

WarmPoolConfig WarmPoolManager::get_config() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->config;
}

void WarmPoolManager::start() {
    if (impl_->running.exchange(true)) {
        return;  // Already running
    }

    impl_->background_thread = std::thread([this] {
        impl_->background_loop();
    });
}

void WarmPoolManager::stop() {
    impl_->stop();
}

bool WarmPoolManager::is_running() const {
    return impl_->running;
}

WarmPoolManager::Stats WarmPoolManager::get_stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto stats = impl_->stats;
    stats.current_warm_count = 0;
    stats.current_in_use_count = 0;
    for (const auto& [id, instance] : impl_->instances) {
        if (instance.state == WarmInstance::State::Ready) {
            stats.current_warm_count++;
        } else if (instance.state == WarmInstance::State::InUse) {
            stats.current_in_use_count++;
        }
    }
    return stats;
}

void WarmPoolManager::reset_stats() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stats = Stats{};
}

void WarmPoolManager::terminate_all() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (auto& [id, instance] : impl_->instances) {
        instance.state = WarmInstance::State::Terminated;
        impl_->stats.total_instances_terminated++;
    }
    impl_->instances.clear();
}

std::unique_ptr<WarmPoolManager> create_warm_pool_manager() {
    return std::make_unique<WarmPoolManager>(WarmPoolConfig{});
}

std::unique_ptr<WarmPoolManager> create_warm_pool_manager(WarmPoolConfig config) {
    return std::make_unique<WarmPoolManager>(std::move(config));
}

}  // namespace neamc::scaling
