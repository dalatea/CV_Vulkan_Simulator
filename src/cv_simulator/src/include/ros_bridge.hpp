#pragma once
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <thread>
#include <mutex>
#include <cstring>

struct CameraBridge {
    rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr pub;
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr sub;
    geometry_msgs::msg::Twist last_cmd;
    std::string name;
};

class RosImageBridge {
public:
  RosImageBridge()
  {
    rclcpp::init(0, nullptr);
    node_ = std::make_shared<rclcpp::Node>("vulkan_image_pub");

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

  void addCamera(const std::string& cameraName)
  {
      CameraBridge cam;
      cam.name = cameraName;

      std::string imageTopic = "/sim/" + cameraName + "/image";
      std::string cmdTopic   = "/sim/" + cameraName + "/cmd";

      cam.pub = node_->create_publisher<sensor_msgs::msg::Image>(
          imageTopic,
          rclcpp::SensorDataQoS()
      );

      int idx = static_cast<int>(cameras_.size());
      cam.sub = node_->create_subscription<geometry_msgs::msg::Twist>(
          cmdTopic, 10,
          [this, idx](geometry_msgs::msg::Twist::SharedPtr msg) {
              std::lock_guard<std::mutex> lock(cmd_mutex_);
              if (idx < static_cast<int>(cameras_.size())) {
                  cameras_[idx].last_cmd = *msg;
              }
          }
      );

      cameras_.push_back(std::move(cam));
  }

  int cameraCount() const { return static_cast<int>(cameras_.size()); }

  void publishBGRA8(int camIndex, uint32_t width, uint32_t height, const void* data, size_t bytes)
  {
    if (camIndex < 0 || camIndex >= static_cast<int>(cameras_.size())) return;

    auto msg = sensor_msgs::msg::Image();
    msg.header.stamp = node_->get_clock()->now();
    msg.header.frame_id = cameras_[camIndex].name;
    msg.width = width; 
    msg.height = height;
    msg.encoding = "bgra8";
    msg.is_bigendian = false;
    msg.step = width * 4;
    msg.data.resize(bytes);
    std::memcpy(msg.data.data(), data, bytes);
    cameras_[camIndex].pub->publish(std::move(msg));
  }

  void publishBGRA8(uint32_t width, uint32_t height,
                    const void* data, size_t bytes)
  {
    publishBGRA8(0, width, height, data, bytes);
  }

  geometry_msgs::msg::Twist getLastCmd(int camIndex = 0) {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    if (camIndex < 0 || camIndex >= static_cast<int>(cameras_.size())) {
      return geometry_msgs::msg::Twist();
    }
    return cameras_[camIndex].last_cmd;
  }

  geometry_msgs::msg::Twist consumeLastCmd(int camIndex = 0) {
    std::lock_guard<std::mutex> lock(cmd_mutex_);
    if (camIndex < 0 || camIndex >= static_cast<int>(cameras_.size())) {
      return geometry_msgs::msg::Twist();
    }
    auto cmd = cameras_[camIndex].last_cmd;
    cameras_[camIndex].last_cmd = geometry_msgs::msg::Twist();
    return cmd;
  }
private:
  std::shared_ptr<rclcpp::Node> node_;
  std::vector<CameraBridge> cameras_;

  std::thread spin_;

  std::mutex cmd_mutex_;
};
