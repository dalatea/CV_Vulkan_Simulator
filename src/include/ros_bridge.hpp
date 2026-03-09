#pragma once
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <thread>
#include <mutex>
#include <cstring>

class RosImageBridge {
public:
  RosImageBridge()
  {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<rclcpp::Node>("vulkan_image_pub");
    pub_ = node_->create_publisher<sensor_msgs::msg::Image>(
      "/sim/image", 
      rclcpp::SensorDataQoS()
    );
    sub_ = node_->create_subscription<geometry_msgs::msg::Twist>(
      "/sim/camera_cmd", 10,
      [this](geometry_msgs::msg::Twist::SharedPtr msg) {
        std::lock_guard<std::mutex> lock(cmd_mutex_);
        last_cmd_ = *msg;
      });
    spin_ = std::thread([this]{
      rclcpp::executors::SingleThreadedExecutor exec;
      exec.add_node(node_);
      exec.spin();
    });
  }

  ~RosImageBridge() {
    rclcpp::shutdown();
    
    if (spin_.joinable()) spin_.join();
  }

  void publishBGRA8(uint32_t width, uint32_t height, const void* data, size_t bytes)
  {
    auto msg = sensor_msgs::msg::Image();
    msg.header.stamp = node_->get_clock()->now();
    msg.header.frame_id = "sim_camera";
    msg.width = width; 
    msg.height = height;
    msg.encoding = "bgra8";
    msg.is_bigendian = false;
    msg.step = width * 4;
    msg.data.resize(bytes);
    std::memcpy(msg.data.data(), data, bytes);
    pub_->publish(std::move(msg));
  }

  geometry_msgs::msg::Twist getLastCmd() {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    return last_cmd_;
  }

  geometry_msgs::msg::Twist consumeLastCmd() {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    auto cmd = last_cmd_;

    last_cmd_ = geometry_msgs::msg::Twist();
    return cmd;
  }
private:
  std::shared_ptr<rclcpp::Node> node_;
  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub_;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr sub_;

  std::thread spin_;

  std::mutex cmd_mutex_;
  geometry_msgs::msg::Twist last_cmd_;
};
