#define EIGEN_RUNTIME_NO_MALLOC
#include <sdf_contact_estimation/sdf_contact_estimation.h>
#include <sdf_contact_estimation/robot_model/shape_model.h>

#include <benchmark/benchmark.h>
#include <random>

constexpr size_t num_configurations = 2000;

static void BM_PoseEstimation(benchmark::State& state, const std::vector<double>& joint_state_vec) {
  // Setup
  ros::NodeHandle pnh("~");

  bool compute_contact_information = pnh.param("compute_contact_information", false);

  // Robot model
  auto shape_model = std::make_shared<sdf_contact_estimation::ShapeModel>(pnh);
  std::vector<double> default_state = pnh.param("default_state",
                                                std::vector<double>(shape_model->jointNames().size(), 0.0));
  shape_model->updateJointPositions(default_state);
  std::vector<std::string> joints = pnh.param("joints", std::vector<std::string>());
  std::unordered_map<std::string, double> joint_update;
  for (unsigned int j = 0; j < joints.size(); ++j) {
    joint_update.emplace(joints[j], joint_state_vec[j]);
  }

  // SDF
  auto sdf_model = std::make_shared<sdf_contact_estimation::SdfModel>(pnh);
  ros::NodeHandle sdf_model_nh(pnh, "sdf_map");
  sdf_model->loadFromServer(sdf_model_nh);

  // Contact estimation
  hector_pose_prediction_interface::PosePredictor<double>::Ptr pose_predictor =
      std::make_shared<sdf_contact_estimation::SDFContactEstimation>(pnh, shape_model, sdf_model);

  // Test parameters
  // Vary the robot pose
  std::default_random_engine engine(0); // NOLINT(cert-msc51-cpp)
  std::uniform_real_distribution<double> pose_x_distribution(0, 2.4);
  std::uniform_real_distribution<double> pose_z_distribution(-0.1, 0.5);


  std::vector<Eigen::Isometry3d, Eigen::aligned_allocator<Eigen::Isometry3d>> robot_poses;
  robot_poses.reserve(num_configurations);
  for (size_t i = 0; i < num_configurations; ++i) {
    Eigen::Isometry3d robot_pose;
    robot_pose = Eigen::AngleAxisd(0, Eigen::Vector3d::UnitZ())
                 * Eigen::AngleAxisd(0, Eigen::Vector3d::UnitY())
                 * Eigen::AngleAxisd(0, Eigen::Vector3d::UnitX());
    robot_pose.translation() = Eigen::Vector3d(pose_x_distribution(engine), 0.0, pose_z_distribution(engine));
    robot_poses.push_back(robot_pose);
  }

  // Benchmark
  double failed_estimations = 0;
  size_t index = 0;
  for (auto _ : state) {
    // This code gets timed
    pose_predictor->robotModel()->updateJointPositions(joint_update);

    hector_math::Pose<double> robot_pose(robot_poses[index]);
    double stability;
    if (!compute_contact_information) {
      stability = pose_predictor->predictPose(robot_pose);
    } else {
      hector_pose_prediction_interface::SupportPolygon<double> support_polygon;
      hector_pose_prediction_interface::ContactInformation<double> contact_information;
      stability = pose_predictor->predictPoseAndContactInformation(robot_pose, support_polygon, contact_information);
    }

    if (std::isnan(stability)) {
      ++failed_estimations;
    }

    if (++index == robot_poses.size()) index = 0;
  }
  state.counters["Failed"] = failed_estimations;
  state.counters["Failed (%)"] = benchmark::Counter(100 * failed_estimations, benchmark::Counter::kAvgIterations);
}

int main(int argc, char** argv) {
  // Initialize ROS
  ros::init(argc, argv, "pose_estimation_benchmark");

  // Initialize Benchmark
  // Set up benchmarks
  std::vector<std::pair<std::string, std::vector<double>>> test_inputs
  {
    std::make_pair("BM_PoseEstimation_ConfigFlat", std::vector<double>{0, 0}),
    std::make_pair("BM_PoseEstimation_ConfigTank", std::vector<double>{-0.5, -0.5}),
    std::make_pair("BM_PoseEstimation_ConfigS", std::vector<double>{-0.5, -2.4}),
    std::make_pair("BM_PoseEstimation_ConfigHigh", std::vector<double>{-2.4, -2.4})
  };
  for (const auto& test: test_inputs) {
    benchmark::RegisterBenchmark(test.first.c_str(), BM_PoseEstimation, test.second)->Unit(benchmark::kMillisecond)->Iterations(num_configurations * 2.0);
  }

  // Benchmark does not like the additional ros command line arguments, so cut them out
  int argc2 = 1;
  char* argv2[1]{argv[0]};
  benchmark::Initialize(&argc2, argv2);
  benchmark::RunSpecifiedBenchmarks();
}
