#include "impact_point_estimator/impact_point_estimator.hpp"
#include <chrono>

using namespace std::chrono_literals;

namespace impact_point_estimator
{
  ImpactPointEstimator::ImpactPointEstimator(const rclcpp::NodeOptions &options)
      : ImpactPointEstimator("", options) {}

  ImpactPointEstimator::ImpactPointEstimator(const std::string &name_space, const rclcpp::NodeOptions &options)
      : rclcpp::Node("impact_point_estimator", name_space, options),
        is_predicting_(true),
        filter_(),
        prediction_()
  {
    RCLCPP_INFO(this->get_logger(), "impact_point_estimatorの初期化");

    motor_pos_ = this->get_parameter("motor_pos").as_double();
    offset_time_ = this->get_parameter("offset_time").as_double();
    curve_points_num_ = this->get_parameter("curve_points_num").as_int();
    standby_pose_x_ = this->get_parameter("standby_pose_x").as_double();
    standby_pose_y_ = this->get_parameter("standby_pose_y").as_double();
    reroad_ = this->get_parameter("reroad").as_double();
    target_height_ = this->get_parameter("target_height").as_double();

    // サブスクライバーの設定
    subscription_ = this->create_subscription<visualization_msgs::msg::Marker>(
        "tennis_ball", 10, std::bind(&ImpactPointEstimator::listener_callback, this, std::placeholders::_1));

    // パブリッシャーの設定
    publisher_ = this->create_publisher<visualization_msgs::msg::Marker>("/fitted_curve", 10);
    points_publisher_ = this->create_publisher<visualization_msgs::msg::Marker>("/fitted_points", 10);
    pose_publisher_ = this->create_publisher<geometry_msgs::msg::Pose2D>("/target_pose", 10);
    motor_pos_publisher_ = this->create_publisher<std_msgs::msg::Float64>("motor/pos", 10);

    last_point_time_ = std::chrono::steady_clock::now();
  }

  void ImpactPointEstimator::listener_callback(const visualization_msgs::msg::Marker::SharedPtr msg)
  {
    if (!is_predicting_)
    {
      return;
    }

    geometry_msgs::msg::Point point = msg->pose.position;
    // 現在時刻を取得
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last_point_time_).count();
    last_point_time_ = now;
    if (!prediction_.is_start_time_initialized())
    {
      prediction_.set_start_time(now);
    }

    // 相対時間を計算
    double time_stamp = prediction_.calculate_relative_time(now);

    if (dt > 0.3)
    {
      clear_data();
      return;
    }
    // ポイントとdtを検証・追加
    if (!filter_.check_point_validity(point, points_, recent_points_, target_height_))
    {
      return;
    }
    points_.emplace_back(point);
    prediction_.add_timestamp(time_stamp);

    if (points_.size() >= static_cast<size_t>(curve_points_num_))
    {
      prediction_.process_points(points_, points_.size(), [this](const PredictionResult &result) {
        if (result.success)
        {
          // 予測が成功したらすぐに着弾地点をpublish
          publish_estimated_impact(result.impact_time, result.x_impact, result.y_impact,
                                   result.x0, result.y0, result.z0, result.vx, result.vy, result.vz);
          // impact_time 後に motor_pos_ をパブリッシュ
          schedule_motor_position(result.impact_time + offset_time_);

          // impact_time + 3秒後に standby_pose と reroad_ をpublish
          double standby_delay = result.impact_time + offset_time_ + 3.0;
          schedule_standby_and_reroad(standby_delay);
          publish_points_marker();
        }
        pause_processing(); });
      clear_data();
    }
  }

  void ImpactPointEstimator::clear_data()
  {
    points_.clear();
    recent_points_.clear();
    prediction_.timestamps_.clear();
    // Prediction内部の開始時刻フラグをリセット
    prediction_.reset_start_time();
  }

  void ImpactPointEstimator::publish_estimated_impact(
      double impact_time, double x_impact, double y_impact,
      double x0, double y0, double z0,
      double vx, double vy, double vz)
  {
    RCLCPP_INFO(this->get_logger(), "着弾時間: %.2f s, 着弾地点: (%.2f, %.2f), height=%.2f", impact_time, x_impact, y_impact, target_height_);

    // 可視化用に軌道をプロット
    std::vector<geometry_msgs::msg::Point> trajectory_points = prediction_.generate_trajectory_points(x0, y0, z0, vx, vy, vz, impact_time);
    publish_curve_marker(trajectory_points);

    // 着弾地点をすぐにpublish
    geometry_msgs::msg::Point final_point;
    final_point.x = x_impact;
    final_point.y = y_impact;
    final_point.z = target_height_;
    publish_final_pose(final_point);
  }

  void ImpactPointEstimator::publish_curve_marker(const std::vector<geometry_msgs::msg::Point> &curve_points)
  {
    visualization_msgs::msg::Marker curve_marker;
    curve_marker.header.frame_id = "map";
    curve_marker.header.stamp = this->get_clock()->now();
    curve_marker.ns = "fitted_curve";
    curve_marker.id = 0;
    curve_marker.type = visualization_msgs::msg::Marker::LINE_STRIP;
    curve_marker.action = visualization_msgs::msg::Marker::ADD;
    curve_marker.scale.x = 0.02; // 線の太さ

    // 色の設定を修正
    curve_marker.color.r = 1.0;
    curve_marker.color.g = 0.0;
    curve_marker.color.b = 0.0;
    curve_marker.color.a = 1.0;

    curve_marker.points = curve_points;
    publisher_->publish(curve_marker);
  }

  void ImpactPointEstimator::publish_points_marker()
  {
    visualization_msgs::msg::Marker points_marker;
    points_marker.header.frame_id = "map";
    points_marker.header.stamp = this->get_clock()->now();
    points_marker.ns = "original_points";
    points_marker.id = 1;
    points_marker.type = visualization_msgs::msg::Marker::SPHERE_LIST;
    points_marker.action = visualization_msgs::msg::Marker::ADD;

    points_marker.scale.x = 0.07;
    points_marker.scale.y = 0.07;
    points_marker.scale.z = 0.07;

    points_marker.color.r = 0.0;
    points_marker.color.g = 1.0;
    points_marker.color.b = 0.5;
    points_marker.color.a = 1.0;

    points_marker.lifetime = rclcpp::Duration(0, 0);

    for (const auto &pt : points_)
    {
      points_marker.points.emplace_back(pt);
    }

    points_publisher_->publish(points_marker);
  }

  void ImpactPointEstimator::publish_final_pose(const geometry_msgs::msg::Point &final_point)
  {
    geometry_msgs::msg::Pose2D target_pose;
    target_pose.x = final_point.x;
    target_pose.y = final_point.y;
    target_pose.theta = 0.0;
    pose_publisher_->publish(target_pose);
    RCLCPP_INFO(this->get_logger(), "Published target_pose: x=%.2f, y=%.2f (height=%.2f), theta=%.2f",
                target_pose.x, target_pose.y, final_point.z, target_pose.theta);
  }

  void ImpactPointEstimator::schedule_motor_position(double delay)
  {
    if (delay > 0.0)
    {
      auto delay_ms = std::chrono::milliseconds(static_cast<int>(delay * 1000));
      timer_ = this->create_wall_timer(
          delay_ms,
          [this]()
          {
            publish_motor_pos(motor_pos_);
            timer_->cancel();
          });
    }
    else
    {
      publish_motor_pos(motor_pos_);
    }
  }

  // impact_time + 3秒後にstandby_poseとreroad_をpublishする
  void ImpactPointEstimator::schedule_standby_and_reroad(double delay)
  {
    if (delay > 0.0)
    {
      auto delay_ms = std::chrono::milliseconds(static_cast<int>(delay * 1000));
      standby_timer_ = this->create_wall_timer(
          delay_ms,
          [this]()
          {
            // standby_poseのpublish
            geometry_msgs::msg::Pose2D standby_pose;
            standby_pose.x = standby_pose_x_;
            standby_pose.y = standby_pose_y_;
            standby_pose.theta = 0.0;
            pose_publisher_->publish(standby_pose);
            // reroad_をpublish
            std_msgs::msg::Float64 motor_msg;
            motor_msg.data = reroad_;
            motor_pos_publisher_->publish(motor_msg);

            standby_timer_->cancel();
          });
    }
    else
    {
      geometry_msgs::msg::Pose2D standby_pose;
      standby_pose.x = standby_pose_x_;
      standby_pose.y = standby_pose_y_;
      standby_pose.theta = 0.0;
      pose_publisher_->publish(standby_pose);

      std_msgs::msg::Float64 motor_msg;
      motor_msg.data = reroad_;
      motor_pos_publisher_->publish(motor_msg);
    }
  }

  void ImpactPointEstimator::pause_processing()
  {
    is_predicting_ = false;
    pause_timer_ = this->create_wall_timer(
        std::chrono::seconds(1),
        std::bind(&ImpactPointEstimator::end_pause, this));
  }

  void ImpactPointEstimator::end_pause()
  {
    if (timer_)
    {
      timer_->cancel();
    }
    is_predicting_ = true;
  }

  void ImpactPointEstimator::publish_motor_pos(double angle_rad)
  {
    auto message = std_msgs::msg::Float64();
    message.data = angle_rad;
    motor_pos_publisher_->publish(message);
    RCLCPP_INFO(this->get_logger(), "Published motor_pos_: %f rad", angle_rad);
  }

} // namespace impact_point_estimator
