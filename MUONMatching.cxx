#include "MUONMatching.h"

//_________________________________________________________________________________________________
MUONMatching::MUONMatching() {

  const auto grp = o2::parameters::GRPObject::loadFrom("o2sim_grp.root");
  std::unique_ptr<o2::parameters::GRPObject> mGRP = nullptr;
  mGRP.reset(grp);
  o2::base::Propagator::initFieldFromGRP(grp);
  auto field = static_cast<o2::field::MagneticField*>(TGeoGlobalMagField::Instance()->GetField());

  double position[3] = {0, 0, -61.4};
  mField_z = field->getBz(position);
  printf("B field z = %f [kGauss]\n", mField_z);

  mMCHTrackExtrap.setField();
}


//_________________________________________________________________________________________________
void MUONMatching::Clear() {

  mMFTTracks.clear();
  mMCHTracksDummy.clear();
  mMCHTracks.clear(); //
  mftTrackLabels.clear();
  mchTrackLabels.clear();
}


//_________________________________________________________________________________________________
void MUONMatching::loadMCHTracks() {
// This function populates mMCHTracks (vector of MCH tracks)
//

std::vector<MCHTrack> inputMCHTracks;
// TODO: Load inputMCHTracks & MCLabels from disk

// Propagate MCH Tracks to last MFT Plane & convert to MFT Coordinate system
for (auto track: inputMCHTracks) {
  mMCHTracks.push_back(track);
}

}

//_________________________________________________________________________________________________
void MUONMatching::loadDummyMCHTracks() {

// For now loading MFT Tracks as Dummy MCH tracks
Char_t *trkFile = "mfttracks.root";
TFile *trkFileIn = new TFile(trkFile);
TTree *mftTrackTree = (TTree*) trkFileIn -> Get("o2sim");
std::vector<o2::mft::TrackMFT> trackMFTVec, *trackMFTVecP = &trackMFTVec;
mftTrackTree->SetBranchAddress("MFTTrack", &trackMFTVecP);

o2::dataformats::MCTruthContainer<o2::MCCompLabel>* mcLabels = nullptr;
mftTrackTree -> SetBranchAddress("MFTTrackMCTruth",&mcLabels);

mftTrackTree -> GetEntry(0);
//mftTrackLabels.swap(*mcLabels);

mMCHTracksDummy.swap(trackMFTVec);
std::cout << "Loaded " <<  mMCHTracksDummy.size() << " Fake MCH Tracks" << std::endl;

}



//_________________________________________________________________________________________________
void MUONMatching::loadMFTTracksOut() {
  // Load all MFTTracks and propagate to last MFT Layer;

  Char_t *trkFile = "mfttracks.root";
  TFile *trkFileIn = new TFile(trkFile);
  TTree *mftTrackTree = (TTree*) trkFileIn -> Get("o2sim");
  std::vector<o2::mft::TrackMFT> trackMFTVec, *trackMFTVecP = &trackMFTVec;
  mftTrackTree->SetBranchAddress("MFTTrack", &trackMFTVecP);

  std::vector<int> trackExtClsVec, *trackExtClsVecP = &trackExtClsVec;
  mftTrackTree->SetBranchAddress("MFTTrackClusIdx", &trackExtClsVecP);

  o2::dataformats::MCTruthContainer<o2::MCCompLabel>* mcLabels = nullptr;
  mftTrackTree -> SetBranchAddress("MFTTrackMCTruth",&mcLabels);

  mftTrackTree -> GetEntry(0);
  mftTrackLabels = *mcLabels;
  mMFTTracks.swap(trackMFTVec);
  mtrackExtClsIDs.swap(trackExtClsVec);
  std::cout << "Loaded " <<  mMFTTracks.size() << " MFT Tracks" << std::endl;

  for (auto& track: mMFTTracks) {
    track.setParameters(track.getOutParam().getParameters());
    track.setCovariances(track.getOutParam().getCovariances());
    track.setZ(track.getOutParam().getZ());
    track.propagateToZhelix(sMatchingPlaneZ,mField_z);
  }

loadMFTClusters();


}

//_________________________________________________________________________________________________
void MUONMatching::loadMFTClusters() {

  using o2::itsmft::CompClusterExt;

  // Geometry and matrix transformations
  std::string inputGeom = "o2sim_geometry.root";
  o2::base::GeometryManager::loadGeometry(inputGeom);
  auto gman = o2::mft::GeometryTGeo::Instance();
  gman->fillMatrixCache(o2::utils::bit2Mask(o2::TransformType::L2G));

  // Cluster pattern dictionary
  std::string dictfile = "MFTdictionary.bin";
  o2::itsmft::TopologyDictionary dict;
  std::ifstream file(dictfile.c_str());
  if (file.good()) {
    printf("Running with dictionary: %s \n", dictfile.c_str());
    dict.readBinaryFile(dictfile);
  } else {
    printf("Can not run without dictionary !\n");
    return;
  }

  // Clusters

  TFile fileC("mftclusters.root");
  TTree *clsTree = (TTree*)fileC.Get("o2sim");
  std::vector<CompClusterExt> clsVec, *clsVecP = &clsVec;
  clsTree->SetBranchAddress("MFTClusterComp", &clsVecP);
  o2::dataformats::MCTruthContainer<o2::MCCompLabel>* clsLabels = nullptr;
  if (clsTree->GetBranch("MFTClusterMCTruth")) {
    clsTree->SetBranchAddress("MFTClusterMCTruth", &clsLabels);
  } else {
    printf("No Monte-Carlo information in this file\n");
    return;
  }

  int nEntries = clsTree->GetEntries();
  printf("Number of entries in clusters tree %d \n", nEntries);

  clsTree->GetEntry(0);

  int nClusters = clsVec.size();
  printf("Number of clusters %d \n", nClusters);

 auto clusterId = 0;
 for (auto& c : clsVec) {
     auto chipID = c.getChipID();
     auto pattID = c.getPatternID();
     Point3D<float> locC;
     float sigmaX2 = o2::mft::ioutils::DefClusError2Row, sigmaY2 = o2::mft::ioutils::DefClusError2Col;

     if (pattID != o2::itsmft::CompCluster::InvalidPatternID) {
       //sigmaX2 = dict.getErr2X(pattID); // ALPIDE local X coordinate => MFT global X coordinate (ALPIDE rows)
       //sigmaY2 = dict.getErr2Z(pattID); // ALPIDE local Z coordinate => MFT global Y coordinate (ALPIDE columns)
       // temporary, until ITS bug fix
       sigmaX2 = dict.getErrX(pattID) * dict.getErrX(pattID);
       sigmaY2 = dict.getErrZ(pattID) * dict.getErrZ(pattID);
       if (!dict.isGroup(pattID)) {
         locC = dict.getClusterCoordinates(c);
       } else {
         locC = dict.getClusterCoordinates(c);
       }
     } else {
       locC = dict.getClusterCoordinates(c);
     }

 // Transformation to the local --> global
  auto gloC = gman->getMatrixL2G(chipID) * locC;
 // printf("Cluster %5d   chip ID %03d   evn %2d   mctrk %4d   x,y,z  %7.3f  %7.3f  %7.3f \n", icls, cluster.getChipID(), evnID, trkID, gloC.X(), gloC.Y(), gloC.Z());

  auto clsPoint2D = Point2D<Float_t>(gloC.x(), gloC.y());
  Float_t rCoord = clsPoint2D.R();
  Float_t phiCoord = clsPoint2D.Phi();
  o2::utils::BringTo02PiGen(phiCoord);
  int rBinIndex = 0;//constants::index_table::getRBinIndex(rCoord);
  int phiBinIndex = 0;//constants::index_table::getPhiBinIndex(phiCoord);
  int binIndex = 0;//constants::index_table::getBinIndex(rBinIndex, phiBinIndex);
  MFTCluster& thisCluster = mMFTClusters.emplace_back(gloC.x(), gloC.y(), gloC.z(), phiCoord, rCoord, clusterId, binIndex, sigmaX2, sigmaY2, chipID);
  clusterId++;
 }

}


//_________________________________________________________________________________________________
void MUONMatching::initGlobalTracks() {
// Populates mGlobalMuonTracks using MCH track data

for (auto track: mMCHTracksDummy) { // Running on dummy MCH tracks while MCH Tracks are not loaded
    track.propagateToZhelix(sMatchingPlaneZ,mField_z);
    GlobalMuonTrack gTrack;
    gTrack.setParameters(track.getParameters());
    gTrack.setCovariances(track.getCovariances());
    gTrack.setZ(track.getZ());
    mGlobalMuonTracks.push_back(gTrack);
}


}


//_________________________________________________________________________________________________
GlobalMuonTrack MUONMatching::MCHtoGlobal(MCHTrack& mchTrack) {
// Convert a MCH Track parameters and covariances matrix to the GlobalMuonTrack format.
// Must be called after propagation on the absorber

GlobalMuonTrack convertedTrack;

return convertedTrack;

}


//_________________________________________________________________________________________________
GlobalMuonTrack MUONMatching::matchMFT_MCH_TracksXY(GlobalMuonTrack& mchTrack, MFTTrack& mftTrack) {
// Calculate Matching Chi2 - X and Y positions

  using SVector2 = ROOT::Math::SVector<double, 2>;
  using SMatrix22 = ROOT::Math::SMatrix<double, 2>;
  using SMatrix25 = ROOT::Math::SMatrix<double, 2, 5>;
  using SMatrix52 = ROOT::Math::SMatrix<double, 5, 2>;
  using SMatrix55Std = ROOT::Math::SMatrix<double, 5>;
  using SMatrix55 = ROOT::Math::SMatrix<double, 5, 5, ROOT::Math::MatRepSym<double, 5>>;
  using SMatrix5 = ROOT::Math::SVector<Double_t, 5>;


  SMatrix55 I = ROOT::Math::SMatrixIdentity();
  SMatrix25 H_k;
  SMatrix22 V_k;
  SVector2 m_k(mftTrack.getX(), mftTrack.getY()), r_k_kminus1;
  SMatrix5 GlobalMuonTrackParameters = mchTrack.getParameters();
  SMatrix55 GlobalMuonTrackCovariances = mchTrack.getCovariances();
  V_k(0, 0) = mftTrack.getCovariances()(0,0);
  V_k(1, 1) = mftTrack.getCovariances()(1,1);
  H_k(0, 0) = 1.0;
  H_k(1, 1) = 1.0;

  // Covariance of residuals
  SMatrix22 invResCov = (V_k + ROOT::Math::Similarity(H_k, GlobalMuonTrackCovariances));
  invResCov.Invert();

  // Kalman Gain Matrix
  SMatrix52 K_k = GlobalMuonTrackCovariances * ROOT::Math::Transpose(H_k) * invResCov;

  // Update Parameters
  r_k_kminus1 = m_k - H_k * GlobalMuonTrackParameters; // Residuals of prediction
  GlobalMuonTrackParameters = GlobalMuonTrackParameters + K_k * r_k_kminus1;

  // Update covariances Matrix
  SMatrix55Std updatedCov = (I - K_k * H_k) * GlobalMuonTrackCovariances;

  auto matchChi2Track = ROOT::Math::Similarity(r_k_kminus1, invResCov);

  GlobalMuonTrack matchTrack;
  matchTrack.setZ(mchTrack.getZ());
  matchTrack.setParameters(GlobalMuonTrackParameters);
  matchTrack.setCovariances(GlobalMuonTrackCovariances);
  matchTrack.setMatchingChi2(matchChi2Track);
  return matchTrack;

}

//_________________________________________________________________________________________________
double MUONMatching::matchMFT_MCH_TracksXY(MCHTrack& mchTrack, MFTTrack& mftTrack) {

// Propagate MCH Track to Matching Plane
mMCHTrackExtrap.extrapToZCov(&mchTrack, sMatchingPlaneZ);

// Convert MCH Track to MFT Coordinate System
auto convertedTrack = MCHtoGlobal(mchTrack);

// Get matching Chi2
return matchMFT_MCH_TracksXY(convertedTrack,mftTrack).getMatchingChi2();

}



//_________________________________________________________________________________________________
void MUONMatching::runHeavyMatching() {
// Runs matching over all track combinations

auto mchTrackID=0;

for (auto gTrack: mGlobalMuonTracks) {
auto mftTrackID=0;
std::vector<GlobalMuonTrack> candidates;
std::vector<double> scores;

  for (auto mftTrack: mMFTTracks) {
    auto candidate = matchMFT_MCH_TracksXY(gTrack, mftTrack);
    scores.push_back(candidate.getMatchingChi2());
    candidates.push_back(candidate);
    mftTrackID++;
  }

  std::vector<double>::iterator best_match = std::min_element(scores.begin(), scores.end());
  auto bestMFTMatch = std::distance(scores.begin(), best_match);
  candidates[bestMFTMatch].setBestMFTTrackMatchID(bestMFTMatch);
  mGlobalMuonTracks[mchTrackID]=candidates[bestMFTMatch];
  mftTrackID=0;
  mchTrackID++;

}

}


//_________________________________________________________________________________________________
void MUONMatching::saveGlobalMuonTracks() {

TFile outFile("GlobalMuonTracks.root", "RECREATE");
TTree outTree("o2sim", "Global Muon Tracks");
  std::vector<GlobalMuonTrack>* tracks = &mGlobalMuonTracks;
  MCLabels* trackLabels = new MCLabels();
  outTree.Branch("GlobalMuonTrack", &tracks);
  outTree.Branch("GlobalMuonTrackMCTruth", &trackLabels);
  outTree.Fill();
  outFile.cd();
  outTree.Write();
  outFile.Close();
}





//_________________________________________________________________________________________________
void MUONMatching::fitGlobalMuonTrack(GlobalMuonTrack& gTrack) {

const auto& mftTrack = mMFTTracks[gTrack.getBestMFTTrackMatchID()];

}
