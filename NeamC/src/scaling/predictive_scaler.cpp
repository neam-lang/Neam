// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Predictive Scaler Implementation

#include "neamc/scaling/predictive_scaler.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <numeric>

namespace neamc::scaling {

struct PredictiveScaler::Impl {
    ScalingPolicy policy;
    std::deque<ScalingMetrics> metrics_history;
    std::vector<ScalingEvent> scaling_history;
    int current_replicas = 1;
    bool enabled = true;
    ApplyCallback apply_callback;
    Stats stats{};
    mutable std::mutex mutex;

    static constexpr size_t MAX_METRICS_HISTORY = 10000;
    static constexpr size_t MAX_SCALING_HISTORY = 1000;

    Impl(ScalingPolicy p) : policy(std::move(p)) {}

    double calculate_trend(const std::deque<ScalingMetrics>& data) const {
        if (data.size() < 2) return 0.0;

        // Simple linear regression for trend
        double sum_x = 0, sum_y = 0, sum_xy = 0, sum_xx = 0;
        size_t n = std::min(data.size(), size_t(100));

        for (size_t i = 0; i < n; ++i) {
            double x = static_cast<double>(i);
            double y = data[data.size() - n + i].request_rate;
            sum_x += x;
            sum_y += y;
            sum_xy += x * y;
            sum_xx += x * x;
        }

        double slope = (n * sum_xy - sum_x * sum_y) / (n * sum_xx - sum_x * sum_x);
        return slope;
    }

    std::string detect_pattern(const std::deque<ScalingMetrics>& data) const {
        if (data.size() < 10) return "insufficient_data";

        double trend = calculate_trend(data);
        double avg_rate = 0;
        for (const auto& m : data) {
            avg_rate += m.request_rate;
        }
        avg_rate /= data.size();

        // Check for spike
        if (!data.empty() && data.back().request_rate > avg_rate * 2.0) {
            return "spike";
        }

        if (trend > 0.1) return "increasing";
        if (trend < -0.1) return "decreasing";
        return "stable";
    }

    double calculate_confidence(const std::deque<ScalingMetrics>& data) const {
        if (data.size() < 10) return 0.3;
        if (data.size() < 50) return 0.5;
        if (data.size() < 100) return 0.7;
        return 0.85;
    }
};

PredictiveScaler::PredictiveScaler(ScalingPolicy policy)
    : impl_(std::make_unique<Impl>(std::move(policy))) {}

PredictiveScaler::~PredictiveScaler() = default;

PredictiveScaler::PredictiveScaler(PredictiveScaler&&) noexcept = default;
PredictiveScaler& PredictiveScaler::operator=(PredictiveScaler&&) noexcept = default;

void PredictiveScaler::record_metrics(const ScalingMetrics& metrics) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->metrics_history.push_back(metrics);
    if (impl_->metrics_history.size() > Impl::MAX_METRICS_HISTORY) {
        impl_->metrics_history.pop_front();
    }
}

void PredictiveScaler::record_metrics_batch(const std::vector<ScalingMetrics>& metrics) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& m : metrics) {
        impl_->metrics_history.push_back(m);
    }
    while (impl_->metrics_history.size() > Impl::MAX_METRICS_HISTORY) {
        impl_->metrics_history.pop_front();
    }
}

LoadPrediction PredictiveScaler::predict_load() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    LoadPrediction prediction;
    prediction.confidence = impl_->calculate_confidence(impl_->metrics_history);
    prediction.trend = impl_->detect_pattern(impl_->metrics_history);

    if (impl_->metrics_history.empty()) {
        prediction.predicted_rps_5min = 0;
        prediction.predicted_rps_1hr = 0;
        return prediction;
    }

    // Simple exponential smoothing for short-term prediction
    double alpha = 0.3;
    double smoothed = impl_->metrics_history.back().request_rate;
    for (auto it = impl_->metrics_history.rbegin();
         it != impl_->metrics_history.rend() && std::distance(impl_->metrics_history.rbegin(), it) < 60;
         ++it) {
        smoothed = alpha * it->request_rate + (1 - alpha) * smoothed;
    }

    double trend = impl_->calculate_trend(impl_->metrics_history);
    prediction.predicted_rps_5min = smoothed + trend * 5;
    prediction.predicted_rps_1hr = smoothed + trend * 60;

    // Clamp to reasonable values
    prediction.predicted_rps_5min = std::max(0.0, prediction.predicted_rps_5min);
    prediction.predicted_rps_1hr = std::max(0.0, prediction.predicted_rps_1hr);

    // Generate 24-hour forecast
    prediction.hourly_forecast_24h.resize(24);
    for (int i = 0; i < 24; ++i) {
        prediction.hourly_forecast_24h[i] = smoothed + trend * (i + 1) * 60;
        prediction.hourly_forecast_24h[i] = std::max(0.0, prediction.hourly_forecast_24h[i]);
    }

    return prediction;
}

ScalingDecision PredictiveScaler::decide() const {
    return decide_with_constraints(
        impl_->policy.max_cost_per_hour,
        impl_->policy.target_latency_p95_ms
    );
}

ScalingDecision PredictiveScaler::decide_with_constraints(
    double max_cost_per_hour,
    double max_latency_ms
) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    ScalingDecision decision;
    decision.current_replicas = impl_->current_replicas;
    decision.target_replicas = impl_->current_replicas;
    decision.action = ScalingDecision::Action::NoChange;
    decision.confidence = 0.5;

    if (impl_->metrics_history.empty()) {
        decision.reasoning = "No metrics available";
        return decision;
    }

    const auto& latest = impl_->metrics_history.back();

    // Check CPU utilization
    if (latest.cpu_utilization > impl_->policy.target_cpu_utilization) {
        decision.action = ScalingDecision::Action::ScaleUp;
        int needed = static_cast<int>(std::ceil(
            impl_->current_replicas * latest.cpu_utilization / impl_->policy.target_cpu_utilization
        ));
        decision.target_replicas = std::min(needed, impl_->policy.max_replicas);
        decision.reasoning = "CPU utilization above target";
        decision.confidence = 0.8;
    }
    // Check latency
    else if (latest.latency_p95_ms > max_latency_ms) {
        decision.action = ScalingDecision::Action::ScaleUp;
        decision.target_replicas = std::min(
            impl_->current_replicas + 1,
            impl_->policy.max_replicas
        );
        decision.reasoning = "P95 latency above target";
        decision.confidence = 0.7;
    }
    // Check if we can scale down
    else if (latest.cpu_utilization < impl_->policy.target_cpu_utilization * 0.5 &&
             latest.latency_p95_ms < max_latency_ms * 0.5) {
        // Only scale down if we've been underutilized for a while
        bool sustained_low = true;
        int count = 0;
        for (auto it = impl_->metrics_history.rbegin();
             it != impl_->metrics_history.rend() && count < 10;
             ++it, ++count) {
            if (it->cpu_utilization > impl_->policy.target_cpu_utilization * 0.6) {
                sustained_low = false;
                break;
            }
        }

        if (sustained_low && impl_->current_replicas > impl_->policy.min_replicas) {
            decision.action = ScalingDecision::Action::ScaleDown;
            decision.target_replicas = std::max(
                impl_->current_replicas - 1,
                impl_->policy.min_replicas
            );
            decision.reasoning = "Sustained low utilization";
            decision.confidence = 0.6;
        }
    }

    // Cost estimation
    decision.estimated_cost_change =
        (decision.target_replicas - impl_->current_replicas) * (max_cost_per_hour / impl_->current_replicas);

    return decision;
}

void PredictiveScaler::set_apply_callback(ApplyCallback callback) {
    impl_->apply_callback = std::move(callback);
}

bool PredictiveScaler::apply_decision(const ScalingDecision& decision) {
    if (!impl_->apply_callback) {
        return false;
    }

    bool success = impl_->apply_callback(decision);

    if (success) {
        std::lock_guard<std::mutex> lock(impl_->mutex);

        ScalingEvent event;
        event.timestamp = std::chrono::system_clock::now();
        event.action = decision.action;
        event.from_replicas = impl_->current_replicas;
        event.to_replicas = decision.target_replicas;
        event.reason = decision.reasoning;
        event.cost_impact = decision.estimated_cost_change;

        impl_->scaling_history.push_back(event);
        if (impl_->scaling_history.size() > Impl::MAX_SCALING_HISTORY) {
            impl_->scaling_history.erase(impl_->scaling_history.begin());
        }

        impl_->current_replicas = decision.target_replicas;

        // Update stats
        impl_->stats.total_scaling_events++;
        if (decision.action == ScalingDecision::Action::ScaleUp) {
            impl_->stats.scale_up_events++;
            impl_->stats.last_scale_up = event.timestamp;
        } else if (decision.action == ScalingDecision::Action::ScaleDown) {
            impl_->stats.scale_down_events++;
            impl_->stats.last_scale_down = event.timestamp;
        }
    }

    return success;
}

std::vector<std::string> PredictiveScaler::get_recommendations() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<std::string> recommendations;

    if (impl_->metrics_history.size() < 100) {
        recommendations.push_back("Collect more metrics for better predictions (current: " +
                                 std::to_string(impl_->metrics_history.size()) + ")");
    }

    if (!impl_->metrics_history.empty()) {
        const auto& latest = impl_->metrics_history.back();
        if (latest.cpu_utilization > 0.9) {
            recommendations.push_back("Consider increasing min_replicas for sustained high CPU");
        }
        if (latest.memory_utilization > 0.85) {
            recommendations.push_back("Memory pressure detected - consider larger instance types");
        }
    }

    return recommendations;
}

void PredictiveScaler::set_policy(ScalingPolicy policy) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->policy = std::move(policy);
}

ScalingPolicy PredictiveScaler::get_policy() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->policy;
}

int PredictiveScaler::get_current_replicas() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->current_replicas;
}

void PredictiveScaler::set_current_replicas(int count) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->current_replicas = count;
}

std::vector<ScalingEvent> PredictiveScaler::get_scaling_history(int limit) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    size_t n = std::min(static_cast<size_t>(limit), impl_->scaling_history.size());
    return std::vector<ScalingEvent>(
        impl_->scaling_history.end() - n,
        impl_->scaling_history.end()
    );
}

PredictiveScaler::Stats PredictiveScaler::get_stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

void PredictiveScaler::reset_stats() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stats = Stats{};
}

void PredictiveScaler::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

bool PredictiveScaler::is_enabled() const {
    return impl_->enabled;
}

std::unique_ptr<PredictiveScaler> create_predictive_scaler() {
    return std::make_unique<PredictiveScaler>(ScalingPolicy{});
}

std::unique_ptr<PredictiveScaler> create_predictive_scaler(ScalingPolicy policy) {
    return std::make_unique<PredictiveScaler>(std::move(policy));
}

}  // namespace neamc::scaling
