/**
 * relocperf: main.cpp
 * Copyright (c) Torr Vision Group, University of Oxford, 2017. All rights reserved.
 */

#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <Eigen/Geometry>

#include <tvgutil/filesystem/SequentialPathGenerator.h>
using namespace tvgutil;

//#################### NAMESPACE ALIASES ####################

namespace fs = boost::filesystem;
namespace po = boost::program_options;

//#################### CONSTANTS ####################

static const std::string trainFolderName = "train";
static const std::string validationFolderName = "validation";
static const std::string testFolderName = "test";

//#################### FUNCTIONS ####################

/**
 * \brief Finds the dataset sequences under a root folder.
 *
 *        The assumption is that each valid sequence folder will have both
 *        "train" and "test" subfolders.
 *
 * \param dataset_path The path to a dataset.
 *
 * \return A list of sequence names.
 */
std::vector<std::string> find_sequence_names(const fs::path &dataset_path)
{
  std::vector<std::string> sequences;

  // Iterate over every subfolder of the dataset.
  for(fs::directory_iterator it(dataset_path), end; it != end; ++it)
  {
    fs::path p = it->path();
    fs::path train_path = p / trainFolderName;
    fs::path test_path = p / testFolderName;

    // If the current folder has both a train and test subfolder, we store its name as a valid sequence.
    if(fs::is_directory(train_path) && fs::is_directory(test_path))
    {
      sequences.push_back(p.filename().string());
    }
  }

  // Sort sequence names because the directory iterator does not ensure ordering
  std::sort(sequences.begin(), sequences.end());

  return sequences;
}

/**
 * \brief Reads a rigid pose from disk. The pose has to be stored as a 4x4 row-major matrix.
 *
 * \param fileName The filename.
 *
 * \return The rigid transformation.
 *
 * \throws std::runtime_error if the file is missing.
 */
Eigen::Matrix4f read_pose_from_file(const fs::path &fileName)
{
  if(!fs::is_regular(fileName)) throw std::runtime_error("File not found: " + fileName.string());

  std::ifstream in(fileName.c_str());

  // The Matrix is stored in row-major order on disk.
  Eigen::Matrix4f res;
  in >> res(0, 0) >> res(0, 1) >> res(0, 2) >> res(0, 3);
  in >> res(1, 0) >> res(1, 1) >> res(1, 2) >> res(1, 3);
  in >> res(2, 0) >> res(2, 1) >> res(2, 2) >> res(2, 3);
  in >> res(3, 0) >> res(3, 1) >> res(3, 2) >> res(3, 3);

  return res;
}

/**
 * \brief Computes the angular separation between two rotation matrices.
 *
 * \param r1 The first rotation matrix.
 * \param r2 The second rotation matrix.
 *
 * \return The angular difference between the two transformations.
 */
float angular_separation(const Eigen::Matrix3f &r1, const Eigen::Matrix3f &r2)
{
  // First calculate the rotation matrix which maps r1 to r2.
  Eigen::Matrix3f dr = r2 * r1.transpose();

  // Then compute the corresponding angle-axis transform and return the angle.
  Eigen::AngleAxisf aa(dr);
  return aa.angle();
}

/**
 * \brief Check whether the two poses are similar enough.
 *
 *        The check is performed according to the 7-scenes metric: succeeds if the translation between the
 *        transformations is <= 5cm and the angle is <= 5 deg.
 *
 * \param gtPose   The ground truth pose.
 * \param testPose The pose to be tested.
 *
 * \return Whether the two poses are similar enough.
 */
bool pose_matches(const Eigen::Matrix4f &gtPose, const Eigen::Matrix4f &testPose)
{
  // 7-scenes thresholds.
  static const float translationMaxError = 0.05f;
  static const float angleMaxError = 5.f * M_PI / 180.f;

  const Eigen::Matrix3f gtR = gtPose.block<3, 3>(0, 0);
  const Eigen::Matrix3f testR = testPose.block<3, 3>(0, 0);
  const Eigen::Vector3f gtT = gtPose.block<3, 1>(0, 3);
  const Eigen::Vector3f testT = testPose.block<3, 1>(0, 3);

  // Compute the difference between the transformations.
  const float translationError = (gtT - testT).norm();
  const float angleError = angular_separation(gtR, testR);

  return translationError <= translationMaxError && angleError <= angleMaxError;
}

/**
 * \brief Check whether a pose stored in a text file matches with a ground truth pose, according to the 7-scenes metric.
 *
 * \param gtPose   The ground truth camera pose.
 * \param poseFile The path to a file storing a transformation matrix.
 *
 * \return Whether the pose stored in the file matches the ground truth pose. False if the file is missing.
 */
bool pose_file_matches(const Eigen::Matrix4f &gtPose, const fs::path &poseFile)
{
  if(!fs::is_regular(poseFile)) return false;

  const Eigen::Matrix4f otherPose = read_pose_from_file(poseFile);
  return pose_matches(gtPose, otherPose);
}

/**
 * \brief Struct used to accumulate stats on a dataset sequence.
 */
struct SequenceResults
{
  /** The number of poses in the sequence. */
  int poseCount{0};

  /** The number of frames successfully relocalised. */
  int validPosesAfterReloc{0};

  /** The number of frames successfully relocalised after a round of ICP. */
  int validPosesAfterICP{0};

  /** The number of frames successfully relocalised after a round of ICP+SVM. */
  int validFinalPoses{0};

  /** The sequence of relocalisation results. Same element count as poseCount. */
  std::vector<bool> relocalizationResults;

  /** The sequence of relocalisation results, after ICP. Same element count as poseCount. */
  std::vector<bool> icpResults;

  /** The sequence of relocalisation results, after ICP+SVM. Same element count as poseCount. */
  std::vector<bool> finalResults;
};

/**
 * \brief Process a dataset sequence computing how well the relocaliser performed on it.
 *
 * \param gtFolder    The folder storing the ground truth poses.
 * \param relocFolder The folder storing the relocalisation results.
 *
 * \return The SequenceResults on this sequence.
 */
SequenceResults evaluate_sequence(const fs::path &gtFolder, const fs::path &relocFolder)
{
  SequenceResults res;

  // Create appropriate path generators.
  SequentialPathGenerator gtPathGenerator(gtFolder);
  SequentialPathGenerator relocPathGenerator(relocFolder);

  while(true)
  {
    // Generate the paths to evaluate.
    const fs::path gtPath = gtPathGenerator.make_path("frame-%06i.pose.txt");
    const fs::path relocPath = relocPathGenerator.make_path("pose-%06i.reloc.txt");
    const fs::path icpPath = relocPathGenerator.make_path("pose-%06i.icp.txt");
    const fs::path finalPath = relocPathGenerator.make_path("pose-%06i.final.txt");

    // If the GT file is missing this sequence is over.
    if(!fs::is_regular(gtPath)) break;

    // Read the GT camera pose.
    const Eigen::Matrix4f gtPose = read_pose_from_file(gtPath);

    // Check whether different kinds of relocalisations succeeded.
    bool validReloc = pose_file_matches(gtPose, relocPath);
    bool validICP = pose_file_matches(gtPose, icpPath);
    bool validFinal = pose_file_matches(gtPose, finalPath);

    // Accumulate stats.
    res.validPosesAfterReloc += validReloc;
    res.validPosesAfterICP += validICP;
    res.validFinalPoses += validFinal;

    res.relocalizationResults.push_back(validReloc);
    res.icpResults.push_back(validICP);
    res.finalResults.push_back(validFinal);

    // Increment counters.
    ++res.poseCount;
    gtPathGenerator.increment_index();
    relocPathGenerator.increment_index();
  }

  return res;
}

/**
 * \brief Print a variable allocating to it a certain width on screen.
 */
template <typename T>
void printWidth(const T &item, int width, bool leftAlign = false)
{
  std::cerr << (leftAlign ? std::left : std::right) << std::setw(width) << std::fixed << std::setprecision(2) << item;
}

int main(int argc, char *argv[])
{
  fs::path datasetFolder;
  fs::path relocBaseFolder;
  std::string relocTag;
  bool useValidation = false;
  bool onlineEvaluation = false;

  // Declare some options for the evaluation.
  po::options_description options("Relocperf Options");
  options.add_options()
      ("datasetFolder,d", po::value(&datasetFolder)->required(), "The path to the dataset.")
      ("relocBaseFolder,r", po::value(&relocBaseFolder)->required(), "The path to the folder where the relocalised poses are stored.")
      ("relocTag,t", po::value(&relocTag)->required(), "The tag assigned to the experimento to evaluate.")
      ("useValidation,v", po::bool_switch(&useValidation), "Whether to use the validation sequence to evaluate the relocaliser.")
      ("onlineEvaluation,o", po::bool_switch(&onlineEvaluation), "Whether to save the CSV for the evaluation of online relocalisation.")
      ("help,h", "Print this help message.")
      ;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, options), vm);

  if(vm.count("help"))
  {
    std::cerr << "Usage: " << options << '\n';
    exit(0);
  }

  try
  {
    po::notify(vm);
  }
  catch(const po::error &e)
  {
    std::cerr << "Error parsing the options: " << e.what() << '\n';
    std::cerr << options << '\n';
    exit(1);
  }

  // Find the valid sequences in the dataset folder.
  const std::vector<std::string> sequenceNames = find_sequence_names(datasetFolder);

  std::map<std::string, SequenceResults> results;

  // Evaluate each sequence.
  for(size_t sequenceIdx = 0; sequenceIdx < sequenceNames.size(); ++sequenceIdx)
  {
    const std::string &sequence = sequenceNames[sequenceIdx];

    // Compute the full paths.
    const fs::path gtPath = datasetFolder / sequence / (useValidation ? validationFolderName : testFolderName);
    const fs::path relocFolder = relocBaseFolder / (relocTag + '_' + sequence);

    std::cerr << "Processing sequence " << sequence << " in: " << gtPath << "\t - " << relocFolder << std::endl;
    try
    {
      results[sequence] = evaluate_sequence(gtPath, relocFolder);
    }
    catch(std::runtime_error &)
    {
      std::cerr << "\tSequence has not been evaluated.\n";
    }
  }

  // Print table
  printWidth("Sequence", 15, true);
  printWidth("Poses", 8);
  printWidth("Reloc", 8);
  printWidth("ICP", 8);
  printWidth("Final", 8);
  std::cerr << '\n';

  // Compute percentages for each sequence and print everything.
  for(const auto &sequence : sequenceNames)
  {
    const auto &seqResult = results[sequence];

    float relocPct =
        static_cast<float>(seqResult.validPosesAfterReloc) / static_cast<float>(seqResult.poseCount) * 100.f;
    float icpPct = static_cast<float>(seqResult.validPosesAfterICP) / static_cast<float>(seqResult.poseCount) * 100.f;
    float finalPct = static_cast<float>(seqResult.validFinalPoses) / static_cast<float>(seqResult.poseCount) * 100.f;

    printWidth(sequence, 15, true);
    printWidth(seqResult.poseCount, 8);
    printWidth(relocPct, 8);
    printWidth(icpPct, 8);
    printWidth(finalPct, 8);
    std::cerr << '\n';
  }

  // Compute average performance.
  float relocSum = 0.f;
  float icpSum = 0.f;
  float finalSum = 0.f;

  float relocRawSum = 0.f;
  float icpRawSum = 0.f;
  float finalRawSum = 0.f;
  int poseCount = 0;

  for(const auto &sequence : sequenceNames)
  {
    const auto &seqResult = results[sequence];

    // Non-weighted average, we need percentages
    const float relocPct = static_cast<float>(seqResult.validPosesAfterReloc) / static_cast<float>(seqResult.poseCount);
    const float icpPct = static_cast<float>(seqResult.validPosesAfterICP) / static_cast<float>(seqResult.poseCount);
    const float finalPct = static_cast<float>(seqResult.validFinalPoses) / static_cast<float>(seqResult.poseCount);

    relocSum += relocPct;
    icpSum += icpPct;
    finalSum += finalPct;

    relocRawSum += static_cast<float>(seqResult.validPosesAfterReloc);
    icpRawSum += static_cast<float>(seqResult.validPosesAfterICP);
    finalRawSum += static_cast<float>(seqResult.validFinalPoses);
    poseCount += seqResult.poseCount;
  }

  const float relocAvg = relocSum / sequenceNames.size() * 100.f;
  const float icpAvg = icpSum / sequenceNames.size() * 100.f;
  const float finalAvg = finalSum / sequenceNames.size() * 100.f;

  const float relocWeightedAvg = relocRawSum / poseCount * 100.f;
  const float icpWeightedAvg = icpRawSum / poseCount * 100.f;
  const float finalWeightedAvg = finalRawSum / poseCount * 100.f;

  // Print averages
  std::cerr << '\n';
  printWidth("Average", 15, true);
  printWidth(sequenceNames.size(), 8);
  printWidth(relocAvg, 8);
  printWidth(icpAvg, 8);
  printWidth(finalAvg, 8);
  std::cerr << '\n';
  printWidth("Average (W)", 15, true);
  printWidth(poseCount, 8);
  printWidth(relocWeightedAvg, 8);
  printWidth(icpWeightedAvg, 8);
  printWidth(finalWeightedAvg, 8);
  std::cerr << '\n';

  // Print the weighted average for the parameter search algorithm.
  if(useValidation)
  {
    std::cout << icpWeightedAvg << '\n';
  }

  // Save results of online training-relocalization
  if(onlineEvaluation)
  {
    std::string onlineResultsFilenameStem = relocTag;

    // Process every sequence
    for(auto sequence : sequenceNames)
    {
      auto seqResult = results[sequence];

      std::string outFilename = onlineResultsFilenameStem + '_' + sequence + ".csv";
      std::ofstream out(outFilename);

      // Print header
      out << "FrameIdx; FramePct; Reloc Success; Reloc Sum; Reloc Pct; ICP Success; ICP Sum; ICP Pct\n";

      int relocSum = 0;
      int icpSum = 0;

      for(int poseIdx = 0; poseIdx < seqResult.poseCount; ++poseIdx)
      {
        bool relocSuccess = seqResult.relocalizationResults[poseIdx];
        bool icpSuccess = seqResult.icpResults[poseIdx];

        relocSum += relocSuccess;
        icpSum += icpSuccess;

        float framePct = static_cast<float>(poseIdx) / seqResult.poseCount;
        float relocPct = static_cast<float>(relocSum) / poseIdx;
        float icpPct = static_cast<float>(icpSum) / poseIdx;

        out << poseIdx << "; " << framePct << "; " << relocSuccess << "; " << relocSum << "; " << relocPct << "; "
            << icpSuccess << "; " << icpSum << "; " << icpPct << '\n';
      }
    }
  }

#if 0
  std::cout << "\n\n";

  float colorMin = 40;
  float colorMax = 95;
  std::string relocColor = "ForestGreen";
  std::string icpColor = "CornflowerBlue";

  // Print Latex table row
  std::cout << "\\cellcolor{white} & \\cellcolor{" << relocColor << "} Reloc ";
  for (auto sequence : sequenceNames)
  {
    auto seqResult = results[sequence];

    float relocPct = static_cast<float>(seqResult.validPosesAfterReloc)
    / static_cast<float>(seqResult.poseCount) * 100.f;
    float relocColorPct = (relocPct / 100.f) * (colorMax - colorMin) + colorMin;

    std::cout << "& \\cellcolor{" << relocColor << "!" << relocColorPct << "} "
    << std::setprecision(1) << relocPct << "\\% ";
  }

  std::cout << "\\\\\n\\cellcolor{white}\\multirow{-2}{*}{" << relocTag
  << "} & \\cellcolor{" << icpColor << "} + ICP ";
  for (auto sequence : sequenceNames)
  {
    auto seqResult = results[sequence];

    float icpPct = static_cast<float>(seqResult.validPosesAfterICP)
    / static_cast<float>(seqResult.poseCount) * 100.f;
    float icpColorPct = (icpPct / 100.f) * (colorMax - colorMin) + colorMin;

    std::cout << "& \\cellcolor{" << icpColor << "!" << icpColorPct << "} "
    << std::setprecision(1) << icpPct << "\\% ";
  }

  std::cout << "\\\\\n";
#endif

  return 0;
}