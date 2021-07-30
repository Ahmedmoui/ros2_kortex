from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, DeclareLaunchArgument
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import (
    PathJoinSubstitution,
    LaunchConfiguration,
    ThisLaunchFileDir,
)

kortex_common_configurable_parameters = [
    {"name": "robot_type", "default": None, "description": "Type/series of robot."},
    {
        "name": "robot_ip",
        "default": None,
        "description": "IP address by which the robot can be reached.",
    },
    {
        "name": "description_package",
        "default": "kortex_description",
        "description": "Description package with robot URDF/XACRO files. Usually the argument is not set, it enables use of a custom description.",
    },
    {
        "name": "moveit_config_package",
        "default": "gen3_robotiq_2f_85_move_it_config",
        "description": "MoveIt configuration package for the robot. Usually the argument is not set, it enables use "
        "of a custom config package.",
    },
    {
        "name": "description_file",
        "default": "gen3.xacro",
        "description": "URDF/XACRO description file with the robot.",
    },
    {
        "name": "prefix",
        "default": '""',
        "description": "Prefix of the joint names, useful for multi-robot setup. If changed than also joint names in the controllers' configuration have to be updated.",
    },
    {
        "name": "gripper",
        "default": "robotiq_2f_85",
        "description": "Name of the gripper attached to the arm",
    },
    {
        "name": "use_fake_hardware",
        "default": "false",
        "description": "Start robot with fake hardware mirroring command to its states.",
    },
    {
        "name": "fake_sensor_commands",
        "default": "false",
        "description": "Enable fake command interfaces for sensors used for simple simulations. Used only if 'use_fake_hardware' parameter is true.",
    },
]

kortex_moveit_configurable_parameters = [
    {
        "name": "moveit_config_file",
        "default": "gen3_robotiq_2f_85.srdf.xacro",
        "description": "MoveIt SRDF/XACRO description file with the robot.",
    },
]
kortex_control_configurable_parameters = [
    {
        "name": "runtime_config_package",
        "default": "kortex2_bringup",
        "description": "Package with the controller's configuration in 'config' folder. Usually the argument is not set, it enables use of a custom setup.",
    },
    {
        "name": "controllers_file",
        "default": "kortex_controllers.yaml",
        "description": "YAML file with the controllers configuration.",
    },
    {
        "name": "robot_controller",
        "default": "joint_trajectory_controller",
        "description": "Robot controller to start.",
    },
    {
        "name": "robot_hand_controller",
        "default": "hand_controller",
        "description": "Robot hand controller to start.",
    },
]


def declare_configurable_parameters(parameters):
    return [
        DeclareLaunchArgument(
            param["name"],
            default_value=param["default"],
            description=param["description"],
        )
        for param in parameters
    ]


def set_configurable_parameters(parameters):
    return dict(
        [(param["name"], LaunchConfiguration(param["name"])) for param in parameters]
    )


def generate_launch_description():
    kortex_moveit_launch_arguments = set_configurable_parameters(
        kortex_common_configurable_parameters + kortex_moveit_configurable_parameters
    )
    kortex_moveit_launch_arguments.update({"launch_rviz": "true"})
    kortex_moveit = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([ThisLaunchFileDir(), "kortex_moveit.launch.py"])
        ),
        launch_arguments=kortex_moveit_launch_arguments.items(),
    )

    kortex_control_launch_arguments = set_configurable_parameters(
        kortex_common_configurable_parameters + kortex_control_configurable_parameters
    )
    kortex_control_launch_arguments.update({"launch_rviz": "false"})
    kortex_control = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            PathJoinSubstitution([ThisLaunchFileDir(), "kortex_control.launch.py"])
        ),
        launch_arguments=kortex_control_launch_arguments.items(),
    )
    return LaunchDescription(
        declare_configurable_parameters(kortex_common_configurable_parameters)
        + declare_configurable_parameters(kortex_moveit_configurable_parameters)
        + declare_configurable_parameters(kortex_control_configurable_parameters)
        + [
            kortex_moveit,
            kortex_control,
        ]
    )
