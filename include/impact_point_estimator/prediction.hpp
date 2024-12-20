#pragma once

#include <geometry_msgs/msg/point.hpp>
#include <vector>
#include <tuple>
#include <chrono>
#include <eigen3/Eigen/Dense>
#include <functional>

struct PredictionResult
{
  bool success;
  double impact_time;
  double x_impact;
  double y_impact;
  double x0;
  double y0;
  double z0;
  double vx;
  double vy;
  double vz;
};

class Prediction
{
public:
  void process_points(const std::vector<geometry_msgs::msg::Point> &points, double lidar_to_target_x, double lidar_to_target_y, double lidar_to_target_z, std::function<void(const PredictionResult &)> callback);
  std::vector<geometry_msgs::msg::Point> process_three_points(const std::vector<geometry_msgs::msg::Point> &points, double lidar_to_target_z, std::function<void(const PredictionResult &)> callback);
  bool find_xy_at_target_height(const Eigen::VectorXd &coeffs_x, const Eigen::VectorXd &coeffs_y, const Eigen::VectorXd &coeffs_z, double lidar_to_target_z, double &x_out, double &y_out);

  std::vector<geometry_msgs::msg::Point> fit_cubic_curve(const std::vector<geometry_msgs::msg::Point> &points, Eigen::VectorXd &coeffs_x, Eigen::VectorXd &coeffs_y, Eigen::VectorXd &coeffs_z);
  double calculate_time_to_height(const std::vector<geometry_msgs::msg::Point> &points, std::chrono::steady_clock::time_point start_time, std::chrono::steady_clock::time_point end_time, double lidar_to_target_z);

  std::vector<geometry_msgs::msg::Point> fit_cubic_curve_ransac(const std::vector<geometry_msgs::msg::Point> &points, Eigen::VectorXd &coeffs_x, Eigen::VectorXd &coeffs_y, Eigen::VectorXd &coeffs_z, double threshold = 0.1, int max_iterations = 1000);

  bool fit_ballistic_trajectory(const std::vector<geometry_msgs::msg::Point> &points, const std::vector<double> &times, double &x0, double &y0, double &z0, double &vx, double &vy, double &vz);

  bool calculate_impact_point(double lidar_to_target_x, double lidar_to_target_y, double lidar_to_target_z, double z0, double vz, double &impact_time, double x0, double y0, double vx, double vy, double &x_impact, double &y_impact);

  double calculate_relative_time(std::chrono::steady_clock::time_point current_time);
  void set_start_time(std::chrono::steady_clock::time_point start_time);
  bool is_start_time_initialized() const;
  void add_timestamp(double dt);

  std::vector<geometry_msgs::msg::Point> generate_trajectory_points(double x0, double y0, double z0, double vx, double vy, double vz, double impact_time);

  void reset_start_time()
  {
    start_time_initialized_ = false;
  }

  std::vector<double> timestamps_;

private:
  std::chrono::steady_clock::time_point start_time_;
  bool start_time_initialized_ = false;
};
