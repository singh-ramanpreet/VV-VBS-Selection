/*
 *
 *
 *
 */

#include "ROOT/RDataFrame.hxx"
#include "ROOT/RVec.hxx"

#include "Math/Vector4D.h"
#include "TStopwatch.h"

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <cmath>

using namespace ROOT::VecOps;

const float MUON_MASS = 0.1056583745;
const float ELE_MASS  = 0.000511;
const float W_MASS = 80.385;
const float Z_MASS = 91.1876;

//lepton cuts
const float LEP_PT_VETO_CUT = 20;
const float EL_PT_CUT = 35;
const float EL_ETA_CUT = 2.5;
const float MU_PT_CUT = 35;
const float MU_ETA_CUT = 2.4;

//ak8 jet cuts
const float AK8_MIN_PT = 200;
const float AK8_MAX_ETA = 2.4;
const float AK8_MIN_SDM = 40;
const float AK8_MAX_SDM = 150;

//ak4 jet cuts
//const float AK4_PT_VETO_CUT = 20;
const float AK4_ETA_CUT = 2.4;
const float AK4_PT_CUT = 30;
const float AK4_JJ_MIN_M = 40.0;
const float AK4_JJ_MAX_M = 150.0;
const float VBF_MJJ_CUT= 500;

//cleaning cuts
const float AK8_LEP_DR_CUT = 1.0;
const float AK4_AK8_DR_CUT = 0.8;
const float AK4_DR_CUT = 0.3;

void vbs_flat_ntupler(std::string sample) {

    gErrorIgnoreLevel = kError;

    ROOT::EnableImplicitMT(4);
    const auto poolSize = ROOT::GetImplicitMTPoolSize();
    std::cout << "Pool size: " << poolSize << std::endl;

    const std::string samplesBasePath = "root://cmseos.fnal.gov/";

    std::cout << ">>> Process sample " << sample << std::endl;
    TStopwatch time;
    time.Start();

    std::ifstream sampleListFile (sample, std::ifstream::in);
    std::vector<std::string> sampleList;
    std::string fileLine;

    while (std::getline(sampleListFile, fileLine))
    {
       //std::cout<< fileLine << std::endl;
       sampleList.push_back(samplesBasePath + fileLine);
    }
    sampleListFile.close();

    auto strip_sampleName = [&sample]()
    {
        std::string i(sample);
        auto i0 = i.find_last_of("/");
        auto i1 = i.find_last_of(".");
        return i.substr(i0 + 1, i1 - i0 - 1);
    };
    auto sample_basename = strip_sampleName();

    std::cout << ">>> Number of root files in sample " << sampleList.size() << std::endl;

    ROOT::RDataFrame df("Events", sampleList);
    std::cout << ">>> Number of events: " << *df.Count() << std::endl;

    auto h_count = df.Histo1D({sample_basename.c_str(), sample_basename.c_str(), 1u, 0., 0.}, "event");
    auto print_entries = [&poolSize](TH1D &h_)
    {
        std::cout<< ">>> Entries processed: " << poolSize * h_.GetEntries() << std::endl;
    };
    h_count.OnPartialResult(50000, print_entries);


    auto df2 = df.Filter("HLT_IsoMu24 == true ||\
                          HLT_IsoMu27 == true ||\
                          HLT_Ele27_WPTight_Gsf == true ||\
                          HLT_Ele32_WPTight_Gsf == true ||\
                          HLT_Ele35_WPTight_Gsf == true",
                         "Passes trigger");

    auto df3 = df2.Define("vetoMuons",
                          "Muon_pt > LEP_PT_VETO_CUT && Muon_looseId == true")
                  .Define("tightMuons",
                          "abs(Muon_eta) < MU_ETA_CUT && Muon_pt > MU_PT_CUT && Muon_tightId == true")
                  .Define("nVetoMuons", "Sum(vetoMuons)")
                  .Define("nTightMuons", "Sum(tightMuons)");

    auto df4 = df3.Define("vetoElectrons",
                          "Electron_pt > LEP_PT_VETO_CUT && Electron_cutBased == 1")
                  .Define("tightElectrons",
                          "abs(Electron_eta) < EL_ETA_CUT && Electron_pt > EL_PT_CUT && Electron_cutBased == 4")
                  .Define("nVetoElectrons", "Sum(vetoElectrons)")
                  .Define("nTightElectrons", "Sum(tightElectrons)");

    auto df5 = df4.Filter("!((nTightMuons + nTightElectrons) == 0)", "no tight lepton")
                  .Filter("!((nVetoMuons + nVetoElectrons) > 2)", "more than two veto lepton")
                  .Filter("!(nTightMuons > 0 && nVetoElectrons > 0)", "mix flavor leptons 1")
                  .Filter("!(nTightElectrons > 0 && nVetoMuons > 0)", "mix flavor leptons 2")
                  .Filter("!(nTightMuons == 1 && nVetoMuons > 1)", "more than ??")
                  .Filter("!(nTightElectrons == 1 && nVetoElectrons > 1)", "more than ??");


    auto lepton_idx = [](RVec<int>& tightLeptons, RVec<float>& pt)
    {
        auto pt_sorted_indices = Reverse(Argsort(pt));

        RVec<int> idx = {-1, -1};
        for(auto &i: pt_sorted_indices) {
             if(tightLeptons[i] == 1) {
                 if(idx[0] == -1) idx[0] = int(i);
                 if(idx[1] == -1 && idx[0] != int(i)) idx[1] = int(i);
             }
         }
         return idx;
    };

    auto df6 = df5.Define("mu_idx", lepton_idx, {"tightMuons", "Muon_pt"})
                  .Define("ele_idx", lepton_idx, {"tightElectrons", "Electron_pt"});


    auto cleanedFatJets = [](RVec<int>& fj_id, RVec<float>& fj_eta, RVec<float>& fj_phi,
                             RVec<int>& mu_id, RVec<float>& mu_eta, RVec<float>& mu_phi,
                             RVec<int>& el_id, RVec<float>& el_eta, RVec<float>& el_phi)
    {
        RVec<int> cleanedFatJets_ (fj_id.size(), 0);
        auto isClean = true;
        for(auto &i: Nonzero(fj_id)){
            isClean = true;

            for(auto &j: Nonzero(mu_id)){
                if(DeltaR(fj_eta[i], mu_eta[j], fj_phi[i], mu_phi[j]) < AK8_LEP_DR_CUT) isClean = false;
            }
            if(isClean == false) continue;

            for(auto &j: Nonzero(el_id)){
                if(DeltaR(fj_eta[i], el_eta[j], fj_phi[i], el_phi[j]) < AK8_LEP_DR_CUT) isClean = false;
            }

            if(isClean == true) cleanedFatJets_[i] = 1;
        }
        return cleanedFatJets_;
    };

    auto df7 = df6.Define("goodFatJets", "(FatJet_pt > AK8_MIN_PT || FatJet_pt_jesTotalUp > AK8_MIN_PT ||\
                           FatJet_pt_jesTotalDown > AK8_MIN_PT) &&\
                           (abs(FatJet_eta) < AK8_MAX_ETA) &&\
                           (FatJet_msoftdrop > AK8_MIN_SDM || FatJet_msoftdrop_jesTotalUp > AK8_MIN_SDM ||\
                           FatJet_msoftdrop_jesTotalDown > AK8_MIN_SDM) &&\
                           (FatJet_msoftdrop < AK8_MAX_SDM || FatJet_msoftdrop_jesTotalUp < AK8_MAX_SDM ||\
                           FatJet_msoftdrop_jesTotalDown < AK8_MAX_SDM)")
                  .Define("goodCleanedFatJets", cleanedFatJets, {"goodFatJets", "FatJet_eta", "FatJet_phi",
                                                                 "tightMuons", "Muon_eta", "Muon_phi",
                                                                 "tightElectrons", "Electron_eta", "Electron_phi"});

    auto selectedFatJet_idx = [](RVec<int>& fj_id, RVec<float>& fj_m)
    {
        int idx = -1;
        if (Sum(fj_id) > 0){
            auto sorted_dM_idx = Argsort(abs(fj_m[fj_id] - W_MASS));
            idx = sorted_dM_idx[0];
        }
        return idx;
    };

    auto df8 = df7.Define("selectedFatJet_idx", selectedFatJet_idx, {"goodCleanedFatJets", "FatJet_msoftdrop"});


    auto cleanedJets = [](RVec<int>& jt_id, RVec<float>& jt_eta, RVec<float>& jt_phi,
                          RVec<int>& fj_id, RVec<float>& fj_eta, RVec<float>& fj_phi,
                          RVec<int>& mu_id, RVec<float>& mu_eta, RVec<float>& mu_phi,
                          RVec<int>& el_id, RVec<float>& el_eta, RVec<float>& el_phi)
    {
        RVec<int> cleanedJets_ (jt_id.size(), 0);
        auto isClean = true;
        for(auto &i: Nonzero(jt_id)){
            isClean = true;

            for(auto &j: Nonzero(jt_id)){
                if (i == j) continue;
                if(DeltaR(jt_eta[i], jt_eta[j], jt_phi[i], jt_phi[j]) < AK4_DR_CUT) isClean = false;
            }
            if (isClean == false) continue;

            for(auto &j: Nonzero(fj_id)){
                if(DeltaR(jt_eta[i], fj_eta[j], jt_phi[i], fj_phi[j]) < AK4_AK8_DR_CUT) isClean = false;
            }
            if (isClean == false) continue;

            for(auto &j: Nonzero(mu_id)){
                if(DeltaR(jt_eta[i], mu_eta[j], jt_phi[i], mu_phi[j]) < AK4_DR_CUT) isClean = false;
            }
            if (isClean == false) continue;

            for(auto &j: Nonzero(el_id)){
                if(DeltaR(jt_eta[i], el_eta[j], jt_phi[i], el_phi[j]) < AK4_DR_CUT) isClean = false;
            }

            if(isClean == true) cleanedJets_[i] = 1;
        }
        return cleanedJets_;
    };

    auto df9 = df8.Define("goodJets", "Jet_pt > AK4_PT_CUT ||\
                                       Jet_pt_jesTotalUp > AK4_PT_CUT ||\
                                       Jet_pt_jesTotalDown > AK4_PT_CUT")
                  .Define("nBtag_loose", "Sum(abs(Jet_eta[goodJets]) < 2.4 && Jet_pt[goodJets] > 30 &&\
                                          Jet_btagDeepB[goodJets] > 0.1241)")
                  .Define("nBtag_medium", "Sum(nBtag_loose &&\
                                           Jet_btagDeepB[goodJets] > 0.4184)")
                  .Define("nBtag_tight", "Sum(nBtag_loose &&\
                                          Jet_btagDeepB[goodJets] > 0.7527)")
                  .Define("goodCleanedJets", cleanedJets, {"goodJets", "Jet_eta", "Jet_phi",
                                                           "goodCleanedFatJets", "FatJet_eta", "FatJet_phi",
                                                           "tightMuons", "Muon_eta", "Muon_phi",
                                                           "tightElectrons", "Electron_eta", "Electron_phi"})
                  .Filter("Sum(goodCleanedJets) > 2", "at least 2 AK4 cleaned jets");

    auto selectedJets_idx = [](int selected_fj, RVec<int> jt_id,
                              RVec<float> jt_pt, RVec<float> jt_eta,
                              RVec<float> jt_phi, RVec<float> jt_m)
    {
        auto idx = Combinations(jt_id, jt_id);
        auto jt_pt_1 = Take(jt_pt, idx[0]);
        auto jt_eta_1 = Take(jt_eta, idx[0]);
        auto jt_phi_1 = Take(jt_phi, idx[0]);
        auto jt_m_1 = Take(jt_m, idx[0]);

        auto jt_pt_2 = Take(jt_pt, idx[1]);
        auto jt_eta_2 = Take(jt_eta, idx[1]);
        auto jt_phi_2 = Take(jt_phi, idx[1]);
        auto jt_m_2 = Take(jt_m, idx[1]);

        auto mass_jj = InvariantMasses(jt_pt_1, jt_eta_1, jt_phi_1, jt_m_1,
                                       jt_pt_2, jt_eta_2, jt_phi_2, jt_m_2);

        int selected_bos_j1 = -1, selected_bos_j2 = -1;
        int selected_vbf_j1 = -1, selected_vbf_j2 = -1;

        if (selected_fj == -1){
            auto sorted_mass_bos = Argsort(abs(mass_jj - W_MASS));

            for (auto &i: sorted_mass_bos){
                const int j1_idx = idx[0][i];
                const int j2_idx = idx[1][i];
                if (fabs(jt_eta[j1_idx]) > AK4_ETA_CUT) continue;
                if (fabs(jt_eta[j2_idx]) > AK4_ETA_CUT) continue;
                if (mass_jj[i] < AK4_JJ_MIN_M || mass_jj[i] > AK4_JJ_MAX_M) continue;
                selected_bos_j1 = j1_idx;
                selected_bos_j2 = j2_idx;
                break;
            }
        }

        auto sorted_mass_vbf = Reverse(Argsort(mass_jj));
        for (auto &i: sorted_mass_vbf){
            const int j1_idx = idx[0][i];
            const int j2_idx = idx[1][i];
            if (mass_jj[i] < VBF_MJJ_CUT) continue;
            if (jt_eta[j1_idx] * jt_eta[j2_idx] > 0) continue;
            if (j1_idx == selected_bos_j1 || j1_idx == selected_bos_j2) continue;
            if (j2_idx == selected_bos_j1 || j2_idx == selected_bos_j2) continue;
            selected_vbf_j1 = j1_idx;
            selected_vbf_j2 = j2_idx;
            break;
        }

        return RVec<int> {selected_bos_j1, selected_bos_j2, selected_vbf_j1, selected_vbf_j2};
    };

    auto df10 = df9.Define("selectedJets_idx", selectedJets_idx, {"selectedFatJet_idx", "goodCleanedJets",
                                                                  "Jet_pt", "Jet_eta", "Jet_phi", "Jet_mass"})
                   .Define("bos_j1", "selectedJets_idx[0]")
                   .Define("bos_j2", "selectedJets_idx[1]")
                   .Define("vbf_j1", "selectedJets_idx[2]")
                   .Define("vbf_j2", "selectedJets_idx[3]")
                   .Filter("selectedFatJet_idx != -1 ||\
                            (bos_j1 != -1 && bos_j2 != -1)", "Boosted or Resolved")
                   .Filter("vbf_j1 != -1", "has selected AK4 vbf 1")
                   .Filter("vbf_j2 != -1", "has selected AK4 vbf 2");


    TString select_lepton1 = "mu_idx[0] != -1? %s[mu_idx[0]]: ele_idx[0] != -1? %s[ele_idx[0]]: -999.f";
    TString select_lepton2 = "mu_idx[1] != -1? %s[mu_idx[1]]: ele_idx[1] != -1? %s[ele_idx[1]]: -999.f";

    auto df11 = df10.Define("evt", "event")
                    .Define("lep1_m", "mu_idx[0] != -1? MUON_MASS: ele_idx[0] != -1? ELE_MASS: -999.f")
                    .Define("lep2_m", "mu_idx[1] != -1? MUON_MASS: ele_idx[1] != -1? ELE_MASS: -999.f")
                    .Define("lep1_pt", Form(select_lepton1, "Muon_pt", "Electron_pt"))
                    .Define("lep1_eta", Form(select_lepton1, "Muon_eta", "Electron_eta"))
                    .Define("lep1_phi", Form(select_lepton1, "Muon_phi", "Electron_phi"))
                    .Define("lep1_q", Form(select_lepton1, "Muon_charge", "Electron_charge"))
                    .Define("lep1_iso", Form(select_lepton1, "Muon_pfRelIso04_all", "Electron_pfRelIso03_all"))

                    .Define("lep2_pt", Form(select_lepton2, "Muon_pt", "Electron_pt"))
                    .Define("lep2_eta", Form(select_lepton2, "Muon_eta", "Electron_eta"))
                    .Define("lep2_phi", Form(select_lepton2, "Muon_phi", "Electron_phi"))
                    .Define("lep2_q", Form(select_lepton2, "Muon_charge", "Electron_charge"))
                    .Define("lep2_iso", Form(select_lepton2, "Muon_pfRelIso04_all", "Electron_pfRelIso03_all"))

                    .Define("bos_PuppiAK8_m_sd0_corr", "selectedFatJet_idx > -1? FatJet_msoftdrop[selectedFatJet_idx]: -999.f")
                    .Define("bos_PuppiAK8_pt", "selectedFatJet_idx > -1? FatJet_pt[selectedFatJet_idx]: -999.f");

    const std::vector<std::string> finalVariables = {
        "run", "evt",
        "lep1_pt", "lep1_eta", "lep1_phi", "lep1_m", "lep1_q", "lep1_iso",
        "lep2_pt", "lep2_eta", "lep2_phi", "lep2_m", "lep2_q", "lep2_iso",
        "bos_PuppiAK8_m_sd0_corr", "bos_PuppiAK8_pt",
        "nBtag_loose", "nBtag_medium", "nBtag_tight",
    };

    auto outputFileName = sample_basename + ".root";
    std::cout << ">>> Output Filename: " << outputFileName << std::endl;

    auto dfFinal = df11;
    auto report = dfFinal.Report();

    dfFinal.Snapshot("Events", outputFileName, finalVariables);

    time.Stop();
    report->Print();
    //h_count->Print();
    time.Print();
}