#include "kortex2_gripper_driver/gripper_hardware_interface.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "hardware_interface/types/hardware_interface_type_values.hpp"
#include "rclcpp/rclcpp.hpp"

namespace
{
const rclcpp::Logger LOGGER = rclcpp::get_logger("KortexGripperInterfaceHardware");
}

std::string interfaces_to_string(const std::vector<std::string>& start_interfaces,
                                 const std::vector<std::string>& stop_interfaces)
{
  std::stringstream ss;
  ss << "Start interfaces: " << std::endl << "[" << std::endl;
  for (const auto& start_if : start_interfaces)
  {
    ss << "  " << start_if << std::endl;
  }
  ss << "]" << std::endl;
  ss << "Stop interfaces: " << std::endl << "[" << std::endl;
  for (const auto& stop_if : stop_interfaces)
  {
    ss << "  " << stop_if << std::endl;
  }
  ss << "]" << std::endl;
  return ss.str();
}

namespace kortex2_gripper_driver
{
KortexGripperInterfaceHardware::KortexGripperInterfaceHardware()
  : router_tcp_{ &transport_tcp_,
                 [](k_api::KError err) { cout << "_________ callback error _________" << err.toString(); } }
  , session_manager_{ &router_tcp_ }
  , router_udp_realtime_{ &transport_udp_realtime_,
                          [](k_api::KError err) { cout << "_________ callback error _________" << err.toString(); } }
  , session_manager_real_time_{ &router_udp_realtime_ }
  , base_{ &router_tcp_ }
  , base_cyclic_{ &router_udp_realtime_ }
{
  rclcpp::on_shutdown(std::bind(&KortexGripperInterfaceHardware::stop, this));
}

return_type KortexGripperInterfaceHardware::configure(const hardware_interface::HardwareInfo& info)
{
  RCLCPP_INFO(LOGGER, "Configuring Hardware Interface");
  if (configure_default(info) != return_type::OK)
  {
    return return_type::ERROR;
  }

  info_ = info;

  hw_positions_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_velocities_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_efforts_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_commands_positions_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_commands_velocities_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  hw_commands_efforts_.resize(info_.joints.size(), std::numeric_limits<double>::quiet_NaN());
  control_lvl_.resize(info_.joints.size(), integration_lvl_t::POSITION);

  for (const hardware_interface::ComponentInfo& joint : info_.joints)
  {
    // Gripper supports 1 command interface
    if (joint.command_interfaces.size() != 1)
    {
      RCLCPP_FATAL(LOGGER, "Joint '%s' has %d command interfaces. 2 expected.", joint.name.c_str(),
                   joint.command_interfaces.size());
      return return_type::ERROR;
    }

    if (!(joint.command_interfaces[0].name == hardware_interface::HW_IF_POSITION))
    {
      RCLCPP_FATAL(LOGGER, "Joint '%s' has %s command interface. Expected %s, %s, or %s.", joint.name.c_str(),
                   joint.command_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION,
                   hardware_interface::HW_IF_VELOCITY, hardware_interface::HW_IF_EFFORT);
      return return_type::ERROR;
    }

    if (joint.state_interfaces.size() != 2)
    {
      RCLCPP_FATAL(LOGGER, "Joint '%s'has %d state interfaces. 2 expected.", joint.name.c_str(),
                   joint.state_interfaces.size());
      return return_type::ERROR;
    }

    if (!(joint.state_interfaces[0].name == hardware_interface::HW_IF_POSITION ||
          joint.state_interfaces[0].name == hardware_interface::HW_IF_VELOCITY))
    {
      RCLCPP_FATAL(LOGGER, "Joint '%s' has %s state interface. Expected %s, %s, or %s.", joint.name.c_str(),
                   joint.state_interfaces[0].name.c_str(), hardware_interface::HW_IF_POSITION,
                   hardware_interface::HW_IF_VELOCITY, hardware_interface::HW_IF_EFFORT);
      return return_type::ERROR;
    }
  }

  RCLCPP_INFO(LOGGER, "Hardware Interface successfully configured");
  status_ = hardware_interface::status::CONFIGURED;
  return return_type::OK;
}

std::vector<hardware_interface::StateInterface> KortexGripperInterfaceHardware::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> state_interfaces;
  for (std::size_t i = 0; i < info_.joints.size(); i++)
  {
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_positions_[i]));
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_VELOCITY, &hw_velocities_[i]));
  }

  return state_interfaces;
}

std::vector<hardware_interface::CommandInterface> KortexGripperInterfaceHardware::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> command_interfaces;
  for (std::size_t i = 0; i < info_.joints.size(); i++)
  {
    command_interfaces.emplace_back(hardware_interface::CommandInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &hw_commands_positions_[i]));
  }

  return command_interfaces;
}

return_type KortexGripperInterfaceHardware::prepare_command_mode_switch(const std::vector<std::string>& start_interfaces,
                                                                        const std::vector<std::string>& stop_interfaces)
{
  // Prepare for new command modes
  std::vector<integration_lvl_t> new_modes = {};
  // RCLCPP_ERROR(LOGGER, "*************** prepare_command_mode_switch: %s", interfaces_to_string(start_interfaces,
  // stop_interfaces).c_str());
  for (std::string key : start_interfaces)
  {
    for (std::size_t i = 0; i < info_.joints.size(); i++)
    {
      if (key == info_.joints[i].name + "/" + hardware_interface::HW_IF_POSITION)
      {
        new_modes.push_back(integration_lvl_t::POSITION);
      }
      if (key == info_.joints[i].name + "/" + hardware_interface::HW_IF_VELOCITY)
      {
        new_modes.push_back(integration_lvl_t::VELOCITY);
      }
      if (key == info_.joints[i].name + "/" + hardware_interface::HW_IF_EFFORT)
      {
        new_modes.push_back(integration_lvl_t::EFFORT);
      }
    }
  }
  // Example criteria: All joints must be given new command mode at the same time
  if (new_modes.size() != info_.joints.size())
  {
    return return_type::ERROR;
  }
  // Example criteria: All joints must have the same command mode
  if (!std::all_of(new_modes.begin() + 1, new_modes.end(), [&](integration_lvl_t mode) { return mode == new_modes[0]; }))
  {
    return return_type::ERROR;
  }

  // Stop motion on all relevant joints that are stopping
  for (std::string key : stop_interfaces)
  {
    for (std::size_t i = 0; i < info_.joints.size(); i++)
    {
      if (key.find(info_.joints[i].name) != std::string::npos)
      {
        hw_commands_velocities_[i] = 0;
        hw_commands_efforts_[i] = 0;
        control_lvl_[i] = integration_lvl_t::UNDEFINED;  // Revert to undefined
      }
    }
  }
  // Set the new command modes
  for (std::size_t i = 0; i < info_.joints.size(); i++)
  {
    if (control_lvl_[i] != integration_lvl_t::UNDEFINED)
    {
      // Something else is using the joint! Abort!
      return return_type::ERROR;
    }
    control_lvl_[i] = new_modes[i];
  }
  return return_type::OK;
}

return_type KortexGripperInterfaceHardware::start()
{
  RCLCPP_INFO(LOGGER, "Connecting to robot at %s ...", info_.hardware_parameters["robot_ip"].c_str());

  // The robot's IP address.
  std::string robot_ip = info_.hardware_parameters["robot_ip"];
  // Username to log into the robot controller
  std::string username = "admin";  // TODO: read in info_.hardware_parameters["username"];
  // Password to log into the robot controller
  std::string password = "admin";  // TODO: read in info_.hardware_parameters["password"];

  transport_tcp_.connect(robot_ip, PORT);
  transport_udp_realtime_.connect(robot_ip, PORT_REAL_TIME);

  // Set session data connection information
  auto create_session_info = k_api::Session::CreateSessionInfo();
  create_session_info.set_username(username);
  create_session_info.set_password(password);
  create_session_info.set_session_inactivity_timeout(60000);    // (milliseconds)
  create_session_info.set_connection_inactivity_timeout(2000);  // (milliseconds)

  // Session manager service wrapper
  RCLCPP_INFO(LOGGER, "Creating session for communication");
  session_manager_.CreateSession(create_session_info);
  session_manager_real_time_.CreateSession(create_session_info);
  RCLCPP_INFO(LOGGER, "Session created");

  // gripper_command.set_mode(k_api::Base::GRIPPER_POSITION);
  // auto finger = gripper_command.mutable_gripper()->add_finger();
  // finger->set_finger_identifier(1);
  // k_api::Base::Gripper gripper_feedback;
  // k_api::Base::GripperRequest gripper_request;
  // gripper_request.set_mode(k_api::Base::GRIPPER_POSITION);
  // gripper_feedback = base_.GetMeasuredGripperMovement(gripper_request);
  // finger->set_value(gripper_feedback.finger(0).value());
  // base_.SendGripperCommand(gripper_command);

  auto base_feedback = base_cyclic_.RefreshFeedback();
  float gripper_initial_position = base_feedback.interconnect().gripper_feedback().motor()[0].position();
  // Initialize interconnect command to current gripper position.
  base_command_.mutable_interconnect()->mutable_command_id()->set_identifier(0);

  gripper_command = base_command_.mutable_interconnect()->mutable_gripper_command()->add_motor_cmd();
  gripper_command->set_position(gripper_initial_position);
  gripper_command->set_velocity(0.0);
  gripper_command->set_force(100.0);

  // Set some default values
  for (std::size_t i = 0; i < 1; i++)
  {
    if (std::isnan(hw_positions_[i]))
    {
      // hw_positions_[i] = KortexMathUtil::wrapRadiansFromMinusPiToPi(
      //     KortexMathUtil::toRad(base_feedback.actuators(i).position()));  // rad
    }
    if (std::isnan(hw_velocities_[i]))
    {
      hw_velocities_[i] = 0;
    }
    if (std::isnan(hw_efforts_[i]))
    {
      hw_efforts_[i] = 0;
    }
    if (std::isnan(hw_commands_positions_[i]))
    {
      // hw_commands_positions_[i] = KortexMathUtil::wrapRadiansFromMinusPiToPi(
      //     KortexMathUtil::toRad(base_feedback.actuators(i).position()));  // rad
    }
    if (std::isnan(hw_commands_velocities_[i]))
    {
      hw_commands_velocities_[i] = 0;
    }
    if (std::isnan(hw_commands_efforts_[i]))
    {
      hw_commands_efforts_[i] = 0;
    }
    control_lvl_[i] = integration_lvl_t::UNDEFINED;
  }
  status_ = hardware_interface::status::STARTED;

  RCLCPP_INFO(LOGGER, "System successfully started! %u", control_lvl_[0]);
  return return_type::OK;
}

return_type KortexGripperInterfaceHardware::stop()
{
  RCLCPP_INFO(LOGGER, "Stopping... please wait...");

  // Close API session
  session_manager_.CloseSession();

  // Deactivate the router and cleanly disconnect from the transport object
  router_tcp_.SetActivationStatus(false);
  transport_tcp_.disconnect();

  status_ = hardware_interface::status::STOPPED;

  RCLCPP_INFO(LOGGER, "System successfully stopped!");

  return return_type::OK;
}

return_type KortexGripperInterfaceHardware::read()
{
  k_api::Base::Gripper gripper_feedback;
  k_api::Base::GripperRequest gripper_request;
  gripper_request.set_mode(k_api::Base::GRIPPER_POSITION);
  gripper_feedback = base_.GetMeasuredGripperMovement(gripper_request);
  hw_positions_[0] = gripper_feedback.finger(0).value();

  return return_type::OK;
}

return_type KortexGripperInterfaceHardware::write()
{
  // Kinova::Api::BaseCyclic::Feedback feedback;

  // // Incrementing identifier ensures actuators can reject out of time frames
  // base_command_.set_frame_id(base_command_.frame_id() + 1);
  // if (base_command_.frame_id() > 65535)
  //   base_command_.set_frame_id(0);

  // // update the command for each joint
  // for (std::size_t i = 0; i < actuator_count_; i++)
  // {
  //   // float cmd_degrees = KortexMathUtil::wrapDegreesFromZeroTo360(KortexMathUtil::toDeg(hw_commands_positions_[i]));
  //   // float cmd_vel = KortexMathUtil::toDeg(hw_commands_velocities_[i]);

  //   // base_command_.mutable_actuators(i)->set_position(cmd_degrees);
  //   // base_command_.mutable_actuators(i)->set_velocity(cmd_vel);
  //   base_command_.mutable_actuators(i)->set_command_id(base_command_.frame_id());
  // }

  // // send the command to the robot
  // try
  // {
  //   feedback = base_cyclic_.Refresh(base_command_, 0);
  // }
  // catch (k_api::KDetailedException& ex)
  // {
  //   RCLCPP_ERROR_STREAM(LOGGER, "Kortex exception: " << ex.what());

  //   RCLCPP_ERROR_STREAM(LOGGER, "Error sub-code: " << k_api::SubErrorCodes_Name(
  //                                   k_api::SubErrorCodes((ex.getErrorInfo().getError().error_sub_code()))));
  // }

  return return_type::OK;
}

}  // namespace kortex2_gripper_driver

#include "pluginlib/class_list_macros.hpp"

PLUGINLIB_EXPORT_CLASS(kortex2_gripper_driver::KortexGripperInterfaceHardware, hardware_interface::SystemInterface)
