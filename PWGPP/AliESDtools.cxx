/**************************************************************************
 * Copyright(c) 1998-1999, ALICE Experiment at CERN, All rights reserved. *
 *                                                                        *
 * Author: The ALICE Off-line Project.                                    *
 * Contributors are mentioned in the code where appropriate.              *
 *                                                                        *
 * Permission to use, copy, modify and distribute this software and its   *
 * documentation strictly for non-commercial purposes is hereby granted   *
 * without fee, provided that the above copyright notice appears in all   *
 * copies and that both the copyright notice and this permission notice   *
 * appear in the supporting documentation. The authors make no claims     *
 * about the suitability of this software for any purpose. It is          *
 * provided "as is" without express or implied warranty.                  *
 **************************************************************************/
///////////////////////////////////////////////////////////////////////////
/// \file AliESDtools.cxx
/// \class AliESDtools
/// \brief Set of tools to define derive ESD variables:
/// \authors marian.ivanov@cern.ch 
/// * Instance of the AliESDtools allow usage of functions in the TTree formulas
/// ** Cache information (e.g mean event properties)
/// ** Set of static function to access pre-calculated variables 
/// * enabling  ESD track functionality in TTree::Draw queries
/// ** Double_t AliESDtools::LoadESD(Int_t entry, Int_t verbose)

/// Example usage:
/*   
  /// 0.) Initialize tool e.g:
  tree = AliXRDPROOFtoolkit::MakeChainRandom("esd.list","esdTree",0,10)
  .L $AliPhysics_SRC/PWGPP/AliESDtools.cxx+
  AliESDtools tools;
  tools.Init(tree);

  /// 1.) Exercise example : trigger exceptional pileup - and check them
  tree->Draw(">>entryList","SPDVertex.fNContributors>100&&Tracks@.GetEntries()/PrimaryVertex.fNContributors>10","entrylist");
  tree->SetEntryList(entryList)
  tree->Scan("AliESDtools::GetTrackMatchEff(0,0):AliESDtools::GetTrackCounters(0,0):AliESDtools::GetTrackCounters(4,0):AliESDtools::GetMeanHisTPCVertexA():AliESDtools::GetMeanHisTPCVertexC():Entry$",\
      "AliESDtools::SCalculateEventVariables(Entry$)")
  /// 2.) Exercise: stream event information
  TTreeSRedirector *pcstream = new TTreeSRedirector("test.root","recreate")
  tools->SetStreamer(pcstream);
  tree->Draw("AliESDtools::SDumpEventVariables()","AliESDtools::SCalculateEventVariables(Entry$)");
  tools->SetStreamer(0);
  delete pcstream;
*/


#include "TStopwatch.h"
#include "TTree.h" 
#include "TChain.h"
#include "TVectorF.h"
#include "AliStack.h"
#include "TDatabasePDG.h"
#include "TParticle.h"
#include "TTreeStream.h"
#include "AliRunLoader.h"
#include "AliTrackReference.h"
#include "AliExternalTrackParam.h"
#include "AliHelix.h"
#include "TCut.h"
#include "AliTreePlayer.h"
#include "THn.h"
#include "TF3.h"
#include "TStatToolkit.h"
#include <stdarg.h>
#include "AliNDLocalRegression.h"
#include "AliESDEvent.h"
#include "AliPIDResponse.h"
#include "TTreeStream.h"
#include "AliESDtools.h"


ClassImp(AliESDtools)
AliESDtools*  AliESDtools::fgInstance;

AliESDtools::AliESDtools():
  fVerbose(0),
  fESDtree(NULL),
  fEvent(NULL),
  fPIDResponse(nullptr),               // PID response parametrization
  fHisTPCVertexA(nullptr),
  fHisTPCVertexC(nullptr),
  fHisTPCVertexACut(nullptr),
  fHisTPCVertexCCut(nullptr),
  fHisTPCVertex(nullptr),
  fHistPhiTPCCounterA(nullptr),         // helper histogram phi counters
  fHistPhiTPCCounterC(nullptr),         // helper histogram for TIdentity tree
  fHistPhiTPCCounterAITS(nullptr),      // helper histogram for TIdentity tree
  fHistPhiTPCCounterCITS(nullptr),      // helper histogram for TIdentity tree
  fHistPhiITSCounterA(nullptr),         // helper histogram for TIdentity tree
  fHistPhiITSCounterC(nullptr),         // helper histogram for TIdentity tree
  fCacheTrackCounters(nullptr),         // track counter
  fCacheTrackTPCCountersZ(nullptr),         // track counter
  fCacheTrackdEdxRatio(nullptr),        // dEdx info counter
  fCacheTrackNcl(nullptr),              // ncl counter
  fCacheTrackChi2(nullptr),             // chi2 counter
  fCacheTrackMatchEff(nullptr),         // matchEff counter
  fLumiGraph(nullptr),                  // graph for the interaction rate info for a run
  fStreamer(nullptr)
{
  fgInstance=this;
}

/// Initialize tool - set ESD address and book histogram counters
/// \param tree - input tree
void AliESDtools::Init(TTree *tree) {
  AliESDtools & tools = *this;
  if (tools.fESDtree) delete tools.fESDtree;
  if (!tools.fEvent) tools.fEvent = new AliESDEvent();
  tools.fESDtree = tree;
  tools.fEvent->ReadFromTree(tree);
  if (fHisTPCVertexA == nullptr) {
    tools.fHisTPCVertexA = new TH1F("hisTPCZA", "hisTPCZA", 1000, -250, 250);
    tools.fHisTPCVertexC = new TH1F("hisTPCZC", "hisTPCZC", 1000, -250, 250);
    tools.fHisTPCVertex = new TH1F("hisTPCZ", "hisTPCZ", 1000, -250, 250);
    tools.fHisTPCVertexACut = new TH1F("hisTPCZACut", "hisTPCZACut", 1000, -250, 250);
    tools.fHisTPCVertexCCut = new TH1F("hisTPCZCCut", "hisTPCZCCut", 1000, -250, 250);
    tools.fHisTPCVertex->SetLineColor(1);
    tools.fHisTPCVertexA->SetLineColor(2);
    tools.fHisTPCVertexC->SetLineColor(4);
    tools.fHisTPCVertexACut->SetLineColor(3);
    tools.fHisTPCVertexCCut->SetLineColor(6);
    tools.fCacheTrackCounters = new TVectorF(20);
    tools.fCacheTrackTPCCountersZ = new TVectorF(8);
    tools.fCacheTrackdEdxRatio = new TVectorF(27);
    tools.fCacheTrackNcl = new TVectorF(20);
    tools.fCacheTrackChi2 = new TVectorF(20);
    tools.fCacheTrackMatchEff = new TVectorF(20);
    // **************************** Event histograms **************************
    fHistPhiTPCCounterA = new TH1F("hPhiTPCCounterC", "control histogram to count tracks on the A side in phi ", 36, 0., 18.);
    fHistPhiTPCCounterC = new TH1F("hPhiTPCCounterA", "control histogram to count tracks on the C side in phi ", 36, 0., 18.);
    fHistPhiTPCCounterAITS = new TH1F("hPhiTPCCounterAITS", "control histogram to count tracks on the A side in phi ", 36, 0., 18.);
    fHistPhiTPCCounterCITS = new TH1F("hPhiTPCCounterCITS", "control histogram to count tracks on the C side in phi ", 36, 0., 18.);
    fHistPhiITSCounterA = new TH1F("hPhiITSCounterA", "control histogram to count tracks on the A side in phi ", 36, 0., 18.);
    fHistPhiITSCounterC = new TH1F("hPhiITSCounterC", "control histogram to count tracks on the C side in phi ", 36, 0., 18.);
  }
}

/// cache TPC event information
/// \return
Int_t AliESDtools::CacheTPCEventInformation(){
  AliESDtools &tools=*this;
  const Int_t kNCRCut=80;
  const Double_t kDCACut=5;
  const Float_t knTrackletCut=1.5;
  // FILL DCA histograms
  tools.fHisTPCVertexA->Reset();
  tools.fHisTPCVertexC->Reset();
  tools.fHisTPCVertexACut->Reset();
  tools.fHisTPCVertexCCut->Reset();
  tools.fHisTPCVertex->Reset();
  Int_t nTracks=tools.fEvent->GetNumberOfTracks();
  Int_t selected=0;
  for (Int_t iTrack=0; iTrack<nTracks; iTrack++){
    AliESDtrack * pTrack = tools.fEvent->GetTrack(iTrack);
    Float_t dcaxy,dcaz;
    if (pTrack== nullptr) continue;
    if (pTrack->IsOn(AliVTrack::kTPCin)==0) continue;
    if (pTrack->GetTPCClusterInfo(3,1)<kNCRCut) continue;
    pTrack->GetImpactParameters(dcaxy,dcaz);
    if (TMath::Abs(dcaxy)>kDCACut) continue;
    pTrack->SetESDEvent(fEvent);
    selected++;
    if ((pTrack->GetNumberOfTRDClusters()/20.+pTrack->GetNumberOfITSClusters())>knTrackletCut){
      tools.fHisTPCVertex->Fill(pTrack->GetTPCInnerParam()->GetZ());
      if (pTrack->GetTgl()>0) tools.fHisTPCVertexACut->Fill(pTrack->GetTPCInnerParam()->GetZ());
      if (pTrack->GetTgl()<0) tools.fHisTPCVertexCCut->Fill(pTrack->GetTPCInnerParam()->GetZ());
    }else{
      if (pTrack->GetTgl()>0) tools.fHisTPCVertexA->Fill(pTrack->GetTPCInnerParam()->GetZ());
      if (pTrack->GetTgl()<0) tools.fHisTPCVertexC->Fill(pTrack->GetTPCInnerParam()->GetZ());

    }
  }
  /// TODO - activating TPC vertex Z finder ?
  /*
  TPCVertexFit(tools.fHisTPCVertex);
  TPCVertexFit(tools.fHisTPCVertexA);
  TPCVertexFit(tools.fHisTPCVertexC);
  TPCVertexFit(tools.fHisTPCVertexACut);
  TPCVertexFit(tools.fHisTPCVertexCCut);
   */
  if (fVerbose&0x10) printf("%d\n",selected); //cacheTPCEventInformation()
  //
  return selected;
}

/// Fit Vertex histogram
/// \param hisVertex
/// TODO - Not finished. Either finish or remove the function
void AliESDtools::TPCVertexFit(TH1F *hisVertex){
  //0.5 cm 1 bin
  // hisVertex=tools.fHisTPCVertexACut;
  TAxis * axis = hisVertex->GetXaxis();
  for (Int_t iBin=5; iBin<axis->GetNbins()-5; iBin++){
    Double_t median10=TMath::Median(10,&(hisVertex->GetArray()[iBin-5]));
    Double_t rms10=TMath::RMS(10,&(hisVertex->GetArray()[iBin-5]));
    Double_t val0=TMath::Mean(3,&(hisVertex->GetArray()[iBin-2]));
    Double_t val1=TMath::Mean(3,&(hisVertex->GetArray()[iBin-1]));
    Double_t val2=TMath::Mean(3,&(hisVertex->GetArray()[iBin+0]));
    if (val1>=val0 && val1>=val2 && val1>3+1.5*median10){
      Double_t xBin=axis->GetBinCenter(iBin);
      printf("Ibin %d\t%f\t%f\t%f\t%f\n", iBin,xBin,val1,median10,rms10);
      hisVertex->Fit("gaus","qnrsame+","qnr",xBin-2,xBin+2);
    }
  }
}



///
/// \param trackMatch    -  input track parameter
/// \param indexSkip     - index to skip  index of track itself
/// \param event         - posinter to the ESD event
/// \param trackType     0 - find closets ITS standalone
///                      1 - find closest track with TPC
///                      2 - closest track with ITS and TPC
/// \param paramType
/// \param paramNearest    - parameter for closest track according trackType
/// \return               - index of the closets track (chi2 distance)
Int_t   AliESDtools::GetNearestTrack(const AliExternalTrackParam * trackMatch, Int_t indexSkip, AliESDEvent*event, Int_t trackType, Int_t paramType, AliExternalTrackParam & paramNearest){
  //
  // Find track with closest chi2 distance  (assume all track ae propagated to the DCA)

  //   paramType = 0 - global track
  //               1 - track at inner wall of TPC
  if (trackMatch==NULL){
    ::Error("AliAnalysisTaskFilteredTree::GetNearestTrack","invalid track pointer");
    return -1;
  }
  Int_t nTracks=event->GetNumberOfTracks();
  const Double_t ktglCut=0.1;
  const Double_t kqptCut=0.4;
  const Double_t kAlphaCut=0.2;
  //
  Double_t chi2Min=100000;
  Int_t indexMin=-1;
  for (Int_t iTrack=0; iTrack<nTracks; iTrack++){
    if (iTrack==indexSkip) continue;
    AliESDtrack *ptrack=event->GetTrack(iTrack);
    if (ptrack==NULL) continue;
    if (trackType==0 && (ptrack->IsOn(0x1)==kFALSE || ptrack->IsOn(0x10)==kTRUE))  continue;     // looks for track without TPC information
    if (trackType==1 && (ptrack->IsOn(0x10)==kFALSE))   continue;                                // looks for tracks with   TPC information
    if (trackType==2 && (ptrack->IsOn(0x1)==kFALSE || ptrack->IsOn(0x10)==kFALSE)) continue;      // looks for tracks with   TPC+ITS information

    if (ptrack->GetKinkIndex(0)<0) continue;              // skip kink daughters
    const AliExternalTrackParam * track=0;                //
    if (paramType==0) track=ptrack;                       // Global track
    if (paramType==1) track=ptrack->GetInnerParam();      // TPC only track at inner wall of TPC
    if (track==NULL) {
      continue;
    }
    // first rough cuts
    // fP3 cut
    if (TMath::Abs((track->GetTgl()-trackMatch->GetTgl()))>ktglCut) continue;
    // fP4 cut
    if (TMath::Abs((track->GetSigned1Pt()-trackMatch->GetSigned1Pt()))>kqptCut) continue;
    // fAlpha cut
    //Double_t alphaDist=TMath::Abs((track->GetAlpha()-trackMatch->GetAlpha()));
    Double_t alphaDist=TMath::Abs(TMath::ATan2(track->Py(),track->Px())-TMath::ATan2(trackMatch->Py(),trackMatch->Py()));
    if (alphaDist>TMath::Pi()) alphaDist-=TMath::TwoPi();
    if (alphaDist>kAlphaCut) continue;
    // calculate and extract track with smallest chi2 distance
    AliExternalTrackParam param(*track);
    if (param.Rotate(trackMatch->GetAlpha())==kFALSE) continue;
    if (param.PropagateTo(trackMatch->GetX(),trackMatch->GetBz())==kFALSE) continue;
    Double_t chi2=trackMatch->GetPredictedChi2(&param);
    if (chi2<chi2Min){
      indexMin=iTrack;
      chi2Min=chi2;
      paramNearest=param;
    }
  }
  return indexMin;

}


/// Function to find match of the TPC standalone tracks and ITS standalone tracks
/// \param esdEvent   -
/// \param esdFriend  - in case ESD friend not available - ITS tracks from vertex to be used
/// \param pcstream   - debug output
/// NOT FINISHED code
/// \TODO 1: find optimal set of cuts to reduce fka matches
/// \TODO 2: sign the track pairs and create "combined TPC-ITS tracks"
void AliESDtools::ProcessITSTPCmatchOut(AliESDEvent *const esdEvent, AliESDfriend *const esdFriend, TTreeStream *pcstream){
  //
  // Process ITS standalone tracks find match with closest TPC(or combined tracks) tracks
  // marian.ivanov@cern.ch
  // 0.) Init variables
  // 1.) GetTrack parameters at TPC inner wall
  // 2.) Match closest TPC  track  (STANDALONE/global) - chi2 match criteria
  //
  // Logic to be used in reco:
  // 1.) Find matching ITSalone->TPCalone
  // 2.) if (!TPCalone.FindClose(TPCother))  TPCalone.Addopt(ITSalone)
  // 3.) ff ((ITSalone.FindClose(Global)==0) CreateGlobaltrack
  const Double_t radiusMatch=84.;    // redius to propagate
  //
  const Double_t dFastPhiCut=0.2;        // 6 sigma (200 MeV) fast angular cut
  const Double_t dFastThetaCut=0.12;     // 6 sigma (200 MeV) fast angular cut
  const Double_t dFastPosPhiCut=0.06;    // 6 sigma (200 MeV) fast angular cut
  const Double_t dFastZCut=6;            // 6 sigma (200 MeV) fast  z difference cut
  const Double_t dFastPtCut=2.;          // 6 sigma (200 MeV) fast 1/pt cut
  const Double_t chi2Cut=100;            // chi2 matching cut
  //
  if (!esdFriend) return;  // not ITS standalone track
  if (esdFriend->TestSkipBit()) return; // friends tracks  not stored
  Int_t nTracks=esdEvent->GetNumberOfTracks();
  Float_t bz = esdEvent->GetMagneticField();
  //
  // 0.) Get parameters in reference radius TPC Inner wall
  //
  //
  TMatrixD vecPosR0(nTracks,6);   // possition and  momentum estimate at reference radius
  TMatrixD vecMomR0(nTracks,6);   //
  TMatrixD vecPosR1(nTracks,6);   // possition and  momentum estimate at reference radius TPC track
  TMatrixD vecMomR1(nTracks,6);   //
  Double_t xyz[3], pxyz[3];      //
  for (Int_t iTrack=0; iTrack<nTracks; iTrack++){
    AliESDtrack *track = esdEvent->GetTrack(iTrack);
    if(!track) continue;
    if (track->GetInnerParam()){
      const AliExternalTrackParam *trackTPC=track->GetInnerParam();
      trackTPC->GetXYZAt(radiusMatch,bz,xyz);
      trackTPC->GetPxPyPzAt(radiusMatch,bz,pxyz);
      for (Int_t i=0; i<3; i++){
        vecPosR1(iTrack,i)=xyz[i];
        vecMomR1(iTrack,i)=pxyz[i];
      }
      vecPosR1(iTrack,3)= TMath::ATan2(xyz[1],xyz[0]);    // phi pos angle
      vecMomR1(iTrack,3)= TMath::ATan2(pxyz[1],pxyz[0]);  // phi mom angle
      vecMomR1(iTrack,4)= trackTPC->GetSigned1Pt();;
      vecMomR1(iTrack,5)= trackTPC->GetTgl();;
    }
    AliESDfriendTrack* friendTrack=esdFriend->GetTrack(iTrack);
    if(!friendTrack) continue;
    if (friendTrack->GetITSOut()){
      const AliExternalTrackParam *trackITS=friendTrack->GetITSOut();
      trackITS->GetXYZAt(radiusMatch,bz,xyz);
      trackITS->GetPxPyPzAt(radiusMatch,bz,pxyz);
      for (Int_t i=0; i<3; i++){
        vecPosR0(iTrack,i)=xyz[i];
        vecMomR0(iTrack,i)=pxyz[i];
      }
      vecPosR0(iTrack,3)= TMath::ATan2(xyz[1],xyz[0]);
      vecMomR0(iTrack,3)= TMath::ATan2(pxyz[1],pxyz[0]);
      vecMomR0(iTrack,4)= trackITS->GetSigned1Pt();;
      vecMomR0(iTrack,5)= trackITS->GetTgl();;
    }
  }
  //
  // 1.) Find closest matching tracks, between the ITS standalone track
  // and  the all other tracks
  //  a.) caltegory  - All
  //  b.) category   - without ITS
  //
  //
  Int_t nTracksPropagated=0;
  AliExternalTrackParam extTrackDummy;
  AliESDtrack           esdTrackDummy;
  AliExternalTrackParam itsAtTPC;
  AliExternalTrackParam itsAtITSTPC;
  for (Int_t iTrack0=0; iTrack0<nTracks; iTrack0++){
    AliESDtrack *track0 = esdEvent->GetTrack(iTrack0);
    if(!track0) continue;
    if (track0->IsOn(AliVTrack::kTPCin)) continue;
    AliESDfriendTrack* friendTrack0=esdFriend->GetTrack(iTrack0);
    if (!friendTrack0) continue;
    //if (!track0->IsOn(AliVTrack::kITSpureSA)) continue;
    //if (!friendTrack0->GetITSOut()) continue;  // is there flag for ITS standalone?
    nTracksPropagated++;
    //
    // 2.) find clostest TPCtrack
    //     a.) all tracks
    Double_t minChi2All=10000000;
    Double_t minChi2TPC=10000000;
    Double_t minChi2TPCITS=10000000;
    Int_t indexAll=-1;
    Int_t indexTPC=-1;
    Int_t indexTPCITS=-1;
    Int_t ncandidates0=0; // n candidates - rough cut
    Int_t ncandidates1=0; // n candidates - rough + chi2 cut
    itsAtTPC=*(friendTrack0->GetITSOut());
    itsAtITSTPC=*(friendTrack0->GetITSOut());
    for (Int_t iTrack1=0; iTrack1<nTracks; iTrack1++){
      AliESDtrack *track1 = esdEvent->GetTrack(iTrack1);
      if(!track1) continue;
      if (!track1->IsOn(AliVTrack::kTPCin)) continue;
      // fast checks
      //
      if (TMath::Abs(vecPosR1(iTrack1,2)-vecPosR0(iTrack0,2))>dFastZCut) continue;
      if (TMath::Abs(vecPosR1(iTrack1,3)-vecPosR0(iTrack0,3))>dFastPosPhiCut) continue;
      if (TMath::Abs(vecMomR1(iTrack1,3)-vecMomR0(iTrack0,3))>dFastPhiCut) continue;
      if (TMath::Abs(vecMomR1(iTrack1,5)-vecMomR0(iTrack0,5))>dFastThetaCut) continue;
      if (TMath::Abs(vecMomR1(iTrack1,4)-vecMomR0(iTrack0,4))>dFastPtCut) continue;
      ncandidates0++;
      //
      const AliExternalTrackParam * param1= track1->GetInnerParam();
      if (!friendTrack0->GetITSOut()) continue;
      AliExternalTrackParam outerITS = *(friendTrack0->GetITSOut());
      if (!outerITS.Rotate(param1->GetAlpha())) continue;
      if (!outerITS.PropagateTo(param1->GetX(),bz)) continue; // assume track close to the TPC inner wall
      Double_t chi2 =  outerITS.GetPredictedChi2(param1);
      if (chi2>chi2Cut) continue;
      ncandidates1++;
      if (chi2<minChi2All){
        minChi2All=chi2;
        indexAll=iTrack1;
      }
      if (chi2<minChi2TPC && track1->IsOn(AliVTrack::kITSin)==0){
        minChi2TPC=chi2;
        indexTPC=iTrack1;
        itsAtTPC=outerITS;
      }
      if (chi2<minChi2TPCITS && track1->IsOn(AliVTrack::kITSin)){
        minChi2TPCITS=chi2;
        indexTPCITS=iTrack1;
        itsAtITSTPC=outerITS;
      }
    }
    //
    AliESDtrack * trackAll= (indexAll>=0)? esdEvent->GetTrack(indexAll):&esdTrackDummy;
    AliESDtrack * trackTPC= (indexTPC>=0)? esdEvent->GetTrack(indexTPC):&esdTrackDummy;
    AliESDtrack * trackTPCITS= (indexTPCITS>=0)? esdEvent->GetTrack(indexTPCITS):&esdTrackDummy;
    (*pcstream)<<"itsTPC"<<
                       "indexAll="<<indexAll<<          // index of closest track (chi2)
                       "indexTPC="<<indexTPC<<          // index of closest TPCalone tracks
                       "indexTPCITS="<<indexTPCITS<<    // index of closest cobined tracks
                       "ncandidates0="<<ncandidates0<<  // number of candidates
                       "ncandidates1="<<ncandidates1<<
                       //
                       "chi2All="<<minChi2All<<         // chi2 of closest  tracks
                       "chi2TPC="<<minChi2TPC<<
                       "chi2TPCITS="<<minChi2TPCITS<<
                       //
                       "track0.="<<track0<<             // ITS standalone tracks
                       "trackAll.="<<trackAll<<         // Closets other track
                       "trackTPC.="<<trackTPC<<         // Closest TPC only track
                       "trackTPCITS.="<<trackTPCITS<<   // closest combined track
                       //
                       "itsAtTPC.="<<&itsAtTPC<<        // ITS track parameters at the TPC alone track  frame
                       "itsAtITSTPC.="<<&itsAtITSTPC<<  // ITS track parameters at the TPC combeined track  frame
                       "\n";
  }
}


//________________________________________________________________________
Int_t AliESDtools::CalculateEventVariables(){
  //AliVEvent *event=InputEvent();
  CacheTPCEventInformation();
  //
  //
  const Int_t kNclTPCCut=60;
  const Int_t kTglCut=1.5;
  const Int_t kDCACut=5;  // 5 cm primary cut
  const Int_t kMindEdxClustersRegion=15;
  const Int_t kPtCut=0.100;
  const Float_t kNTrackletCut=1.5;
  const Float_t kDCAtpcNULL = -10000;
  //
  fCacheTrackCounters->Zero();   // track counter
  fCacheTrackdEdxRatio->Zero(); // dedx info counter
  fCacheTrackNcl->Zero();       // ncl counter
  fCacheTrackChi2->Zero();      // chi2 counter
  fCacheTrackMatchEff->Zero();  // matchEff counter
  //
  if (fHistPhiTPCCounterA)    fHistPhiTPCCounterA->Reset();
  if (fHistPhiTPCCounterC)    fHistPhiTPCCounterC->Reset();
  if (fHistPhiTPCCounterAITS) fHistPhiTPCCounterAITS->Reset();
  if (fHistPhiTPCCounterCITS) fHistPhiTPCCounterCITS->Reset();
  if (fHistPhiITSCounterA)    fHistPhiITSCounterA->Reset();
  if (fHistPhiITSCounterC)    fHistPhiITSCounterC->Reset();
  //
  //
  Int_t nNumberOfTracks = fEvent->GetNumberOfTracks();
  /// TODO -decide should we monitor DCA?
//  Float_t tpcDCAarrPhiA[36][nNumberOfTracks];
//  Float_t tpcDCAarrPhiC[36][nNumberOfTracks];
//  for (Int_t i=0;i<36;i++){
//    for (Int_t j=0;j<nNumberOfTracks;j++){
//      tpcDCAarrPhiA[i][j]=kDCAtpcNULL;
//      tpcDCAarrPhiC[i][j]=kDCAtpcNULL;
//    }
//  }
  //
  // --------------------------------------------------------------
  //      Track LOOP
  // --------------------------------------------------------------
  //
  AliTPCdEdxInfo tpcdEdxInfo;
  TRandom r;
  for (Int_t iTrack=0;iTrack<nNumberOfTracks;++iTrack)
  {

    //
    Double_t eta=-100., phiTPC=0.,sectorNumber=0., tpcdEdx=0., ptotTPC=0.;
    //
    AliESDtrack *track = fEvent->GetTrack(iTrack);
    if (track == NULL) continue;
    //
    Double_t tgl        = track->Pz()/track->Pt();
    Double_t phiGlobal  = track->Phi()-TMath::Pi(); // ?????
    Int_t sign          = track->GetSign();
    Double_t phi        = track->GetParameterAtRadius(85,5,7);
    Double_t sectorNumbertmp = (9*phi/TMath::Pi()+18*(phi<0));
    eta = track->Eta();
    if (TMath::Abs(eta)>0.9) continue;
    Bool_t isOnITS = track->IsOn(AliESDtrack::kITSrefit);
    Bool_t isOnTRD = track->IsOn(AliESDtrack::kTRDrefit);
    Bool_t isOnTPC = track->IsOn(AliESDtrack::kTPCrefit);
    //
    // --------------------------------------------------------------
    //      TPC track information
    // --------------------------------------------------------------
    //
    if (track->GetInnerParam()) {
      tpcdEdx = track->GetTPCsignal();
      ptotTPC = track->GetInnerParam()->GetP();
      phiTPC  = track->GetInnerParam()->GetParameterAtRadius(85,5,7);
      sectorNumber = (9*phiTPC/TMath::Pi()+18*(phiTPC<0));
    }
    //
    // --------------------------------------------------------------
    //      Count only ITS tracks
    // --------------------------------------------------------------
    //
    if ( isOnITS && !isOnTPC ) {
      if (TMath::Abs(phi)>1e-10){
        if (tgl>0) fHistPhiITSCounterA->Fill(sectorNumbertmp);
        if (tgl<0) fHistPhiITSCounterC->Fill(sectorNumbertmp);
      }
    }
    //
    if (!track->GetInnerParam()) continue;  // ????
    if (track->IsOn(AliVTrack::kTPCout)==kFALSE)  continue;  // ????
    (*fCacheTrackCounters)[4]++;      // all TPC track with out flag
    // TPC track counters with DCAZ
    for (Int_t izCut=1; izCut<4; izCut++){
      Float_t impactParam[2];
      track->GetImpactParameters(impactParam[0],impactParam[1]);
      if (TMath::Abs(impactParam[0])>kDCACut) continue;
      if (TMath::Abs(track->GetInnerParam()->GetParameter()[1])<10.*(izCut+1.)) (*fCacheTrackTPCCountersZ)[izCut]++;
      if (TMath::Abs(impactParam[1])<10.*(izCut+1.)) (*fCacheTrackTPCCountersZ)[izCut+4]++;
    }
    //
    //
    Float_t dcaRPhi, dcaZ;
    track->GetImpactParameters(dcaRPhi, dcaZ);
    Int_t nclTPC    = track->GetTPCncls(); if (nclTPC<1) nclTPC=-1;
    Int_t nclITS    = track->GetITSNcls(); if (nclITS<1) nclITS=-1;
    Int_t nclTRD    = track->GetTRDncls(); if (nclTRD<1) nclTRD=-1;
    Int_t nclTOF    = track->IsOn(AliVTrack::kTOFout);
    Double_t chi2TPC = TMath::Sqrt(TMath::Abs(track->GetTPCchi2()/nclTPC));
    Double_t chi2ITS = TMath::Sqrt(TMath::Abs(track->GetITSchi2()));
    Double_t chi2TRD = TMath::Sqrt(TMath::Abs(track->GetTRDchi2()));
    Double_t pTot0   = track->GetP();
    Double_t qP      = track->Charge()/track->P();
    //
    // --------------------------------------------------------------
    //      Some track selections
    // --------------------------------------------------------------
    //
    if (nclTPC<kNclTPCCut) continue;
    if (TMath::Abs(tgl)>kTglCut) continue;
    if (track->Pt()<kPtCut) continue;
    if (TMath::Abs(dcaRPhi)>kDCACut || TMath::Abs(dcaZ)>kDCACut) continue;
    // if ( !( isOnITS||isOnTRD ) ) continue;   // ?????
    //
    // --------------------------------------------------------------
    //      Fill TPC dca information for a given phi bin for each track
    // --------------------------------------------------------------
    //
    //Int_t phiBin = fHistPhi->FindBin(phi)-1;
    Float_t pTPC[2],covTPC[3];          // p[0]=fdTPC; p[1]=fzTPC; cov[0]=fCddTPC; cov[1]=fCdzTPC; cov[2]=fCzzTPC;
    track->GetImpactParametersTPC(pTPC,covTPC);
    //if (tgl>0) tpcDCAarrPhiA[phiBin][iTrack]=pTPC[0];
    //if (tgl<0) tpcDCAarrPhiC[phiBin][iTrack]=pTPC[0];
    //
    // --------------------------------------------------------------
    //      TPC Phi counter
    // --------------------------------------------------------------
    //
    (*fCacheTrackCounters)[5]++;
    if (TMath::Abs(phiTPC)>1e-10){
      if (tgl>0) fHistPhiTPCCounterA->Fill(sectorNumber);
      if (tgl<0) fHistPhiTPCCounterC->Fill(sectorNumber);
      if(isOnITS){
        if (tgl>0) fHistPhiTPCCounterAITS->Fill(sectorNumber);
        if (tgl<0) fHistPhiTPCCounterCITS->Fill(sectorNumber);
      }
    }
    //
    // --------------------------------------------------------------
    //      track counter after pile up  ????
    // --------------------------------------------------------------
    //
    Bool_t pileUpCut=  ( (nclITS>2) || (nclTRD>40) );
    if (pileUpCut==kFALSE) continue;
    if (TMath::Min(chi2TPC,100.)<0) continue;
    (*fCacheTrackCounters)[1]++;
    //
    Bool_t itsOK=track->IsOn(AliVTrack::kITSout) && nclITS>2  && chi2ITS>0;
    Bool_t trdOK=track->IsOn(AliVTrack::kTRDout) && nclTRD>35 && chi2TRD>0;
    Bool_t tofOK=track->IsOn(AliVTrack::kTOFout);
    //
    // --------------------------------------------------------------
    //      number of clusters cut
    // --------------------------------------------------------------
    //
    (*fCacheTrackNcl)[4]+=track->GetTPCncls(0, 63);
    (*fCacheTrackNcl)[5]+=track->GetTPCncls(64, 127);
    (*fCacheTrackNcl)[6]+=track->GetTPCncls(128, 159);
    (*fCacheTrackNcl)[1] += nclTPC;
    (*fCacheTrackChi2)[1]+= (chi2TPC>0) ? TMath::Sqrt(chi2TPC):2;   // sometimes negative chi2?

    if (itsOK && track->GetTPCdEdxInfo(tpcdEdxInfo)){

      Bool_t isOK=(tpcdEdxInfo.GetNumberOfCrossedRows(0)>kMindEdxClustersRegion);
      isOK&=(tpcdEdxInfo.GetNumberOfCrossedRows(1)>kMindEdxClustersRegion);
      isOK&=(tpcdEdxInfo.GetNumberOfCrossedRows(2)>kMindEdxClustersRegion);
      isOK&=((tpcdEdxInfo.GetSignalMax(0)>0) && (tpcdEdxInfo.GetSignalMax(1)>0) && (tpcdEdxInfo.GetSignalMax(2)>0));
      isOK&=((tpcdEdxInfo.GetSignalTot(0)>0) && (tpcdEdxInfo.GetSignalTot(1)>0) && (tpcdEdxInfo.GetSignalTot(2)>0));
      isOK&=(itsOK||trdOK);      // stronger pile-up cut requiring ITS or TRD

      if (isOK) {
        (*fCacheTrackCounters)[6]+=1;         // Counter with accepted TPC dEdx info
        (*fCacheTrackdEdxRatio)[0]+=TMath::Log(tpcdEdxInfo.GetSignalMax(3));
        (*fCacheTrackdEdxRatio)[1]+=TMath::Log(tpcdEdxInfo.GetSignalTot(3));
        (*fCacheTrackdEdxRatio)[2]+=TMath::Log(tpcdEdxInfo.GetSignalMax(0)/tpcdEdxInfo.GetSignalTot(0));
        (*fCacheTrackdEdxRatio)[3]+=TMath::Log(tpcdEdxInfo.GetSignalMax(1)/tpcdEdxInfo.GetSignalTot(1));
        (*fCacheTrackdEdxRatio)[4]+=TMath::Log(tpcdEdxInfo.GetSignalMax(2)/tpcdEdxInfo.GetSignalTot(2));
        (*fCacheTrackdEdxRatio)[5]+=TMath::Log(tpcdEdxInfo.GetSignalMax(3)/tpcdEdxInfo.GetSignalTot(3));
        (*fCacheTrackdEdxRatio)[6]+=TMath::Log(tpcdEdxInfo.GetSignalMax(1)/tpcdEdxInfo.GetSignalMax(0));
        (*fCacheTrackdEdxRatio)[7]+=TMath::Log(tpcdEdxInfo.GetSignalMax(1)/tpcdEdxInfo.GetSignalMax(2));
        (*fCacheTrackdEdxRatio)[8]+=TMath::Log(tpcdEdxInfo.GetSignalTot(1)/tpcdEdxInfo.GetSignalTot(0));
        (*fCacheTrackdEdxRatio)[9]+=TMath::Log(tpcdEdxInfo.GetSignalTot(1)/tpcdEdxInfo.GetSignalTot(2));
        //
        // --------------------------------------------------------------
        //      dEdx counter wrt splines and Bethe bloch
        // --------------------------------------------------------------
        //
        Float_t closestPar[3]={0};    // closestPar[0] --> closest spline, Int_t(closestPar[1]) --> particle index,  closestPar[2] --> corresponding particle mass
        closestPar[2]=AliPID::ParticleMass(AliPID::kPion);   /// default closet mass
        closestPar[0]=50*AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]);  // default dEdx  - assuming pion mass
        /// GetExpecteds(track,closestPar); /// TODO - enable calculation of closest mass
        (*fCacheTrackdEdxRatio)[10]+=TMath::Log(tpcdEdx/closestPar[0]);    // ???
        (*fCacheTrackdEdxRatio)[11]+=TMath::Log((tpcdEdxInfo.GetSignalMax(0)/50.)/AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]));
        (*fCacheTrackdEdxRatio)[12]+=TMath::Log((tpcdEdxInfo.GetSignalMax(1)/50.)/AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]));
        (*fCacheTrackdEdxRatio)[13]+=TMath::Log((tpcdEdxInfo.GetSignalMax(2)/50.)/AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]));
        (*fCacheTrackdEdxRatio)[14]+=TMath::Log((tpcdEdxInfo.GetSignalMax(3)/50.)/AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]));
        (*fCacheTrackdEdxRatio)[15]+=TMath::Log((tpcdEdxInfo.GetSignalTot(0)/50.)/AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]));
        (*fCacheTrackdEdxRatio)[16]+=TMath::Log((tpcdEdxInfo.GetSignalTot(1)/50.)/AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]));
        (*fCacheTrackdEdxRatio)[17]+=TMath::Log((tpcdEdxInfo.GetSignalTot(2)/50.)/AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]));
        (*fCacheTrackdEdxRatio)[18]+=TMath::Log((tpcdEdxInfo.GetSignalTot(3)/50.)/AliExternalTrackParam::BetheBlochAleph(ptotTPC/closestPar[2]));
        (*fCacheTrackdEdxRatio)[19]+=TMath::Log((tpcdEdxInfo.GetSignalMax(0)/50.));
        (*fCacheTrackdEdxRatio)[20]+=TMath::Log((tpcdEdxInfo.GetSignalMax(1)/50.));
        (*fCacheTrackdEdxRatio)[21]+=TMath::Log((tpcdEdxInfo.GetSignalMax(2)/50.));
        (*fCacheTrackdEdxRatio)[22]+=TMath::Log((tpcdEdxInfo.GetSignalMax(3)/50.));
        (*fCacheTrackdEdxRatio)[23]+=TMath::Log((tpcdEdxInfo.GetSignalTot(0)/50.));
        (*fCacheTrackdEdxRatio)[24]+=TMath::Log((tpcdEdxInfo.GetSignalTot(1)/50.));
        (*fCacheTrackdEdxRatio)[25]+=TMath::Log((tpcdEdxInfo.GetSignalTot(2)/50.));
        (*fCacheTrackdEdxRatio)[26]+=TMath::Log((tpcdEdxInfo.GetSignalTot(3)/50.));

      }
    }

    if (itsOK) {  // ITS
      (*fCacheTrackCounters)[0]++;
      (*fCacheTrackNcl)[0] += nclITS;
      (*fCacheTrackChi2)[0] += TMath::Min(TMath::Sqrt(chi2ITS),10.); // cutoff chi2 10
      (*fCacheTrackMatchEff)[2]+=trdOK;
      (*fCacheTrackMatchEff)[3]+=tofOK;
      (*fCacheTrackChi2)[4]+= chi2TPC; // TPC chi2 in case prolongation to ITS
      // long tracks properties
      if (nclITS>4){
        (*fCacheTrackCounters)[7]++;
        (*fCacheTrackNcl)[7] += nclITS;
        (*fCacheTrackChi2)[7]+=TMath::Min(TMath::Sqrt(chi2ITS),10.);
      }
    }
    if (trdOK) {// TRD    ///TODO - why chi2TRD could be smaller than 0?
      (*fCacheTrackCounters)[2]++;
      (*fCacheTrackNcl)[2] += nclTRD;
      (*fCacheTrackChi2)[2] += TMath::Sqrt(chi2TRD);
      (*fCacheTrackMatchEff)[0]+=itsOK;
      (*fCacheTrackChi2)[5]+= chi2TPC; // TPC chi2 in case prolongation to TRD
      if (nclTRD>80){
        (*fCacheTrackCounters)[8]++;
        (*fCacheTrackNcl)[8] += nclTRD;
        (*fCacheTrackChi2)[8]+=TMath::Min(TMath::Sqrt(chi2TRD),10.);
      }
    }
    if (tofOK) {  // TOF
      (*fCacheTrackCounters)[3]++;
      (*fCacheTrackNcl)[3] += 1;   // dummy for the moment
      (*fCacheTrackChi2)[3]+= 1;   //
    }
  } // end of track LOOP
  //
  // ======================================================================
  //  calculate event averages
  // ======================================================================
  //
  for (Int_t i=0; i<9; i++) if ((*fCacheTrackCounters)[i]>0) (*fCacheTrackNcl)[i]/=(*fCacheTrackCounters)[i];
  for (Int_t i=0; i<4; i++) if ((*fCacheTrackCounters)[i]>0) (*fCacheTrackChi2)[i]/=(*fCacheTrackCounters)[i];

  for (Int_t i=4; i<7; i++)  if ((*fCacheTrackCounters)[1]>0) (*fCacheTrackNcl)[i]/=(*fCacheTrackCounters)[1];
  //
  if ((*fCacheTrackCounters)[6]>0){
    for (Int_t i=0; i<27; i++)   (*fCacheTrackdEdxRatio)[i]/=(*fCacheTrackCounters)[6];
  }
  //
  // conditional matching efficiency and chi2
  if ((*fCacheTrackCounters)[0]>0){
    (*fCacheTrackMatchEff)[2]/=(*fCacheTrackCounters)[0];  // TRD if ITS
    (*fCacheTrackMatchEff)[3]/=(*fCacheTrackCounters)[0];  // TOF if ITS
    (*fCacheTrackChi2)[4]/=(*fCacheTrackCounters)[0];
  }
  if ((*fCacheTrackCounters)[2]>0) {
    (*fCacheTrackMatchEff)[0]/=(*fCacheTrackCounters)[2];
    (*fCacheTrackChi2)[5]/=(*fCacheTrackCounters)[2];
  } //ITS if TRD
  (*fCacheTrackCounters)[9]=fEvent->GetNumberOfTracks();  // original number of ESD tracks
  //
  return kTRUE;
}

/// Load ESD - used for the ESD event proper caching - to be executed as first action in TTree::Draw
/// function is static
/// \param entry    - entry number
/// \param verbose  - verbosity
/// \return         - 1  - no load needed, 2 - reset event and load branches
Double_t AliESDtools::LoadESD(Int_t entry, Int_t verbose) {
  static Int_t lastEntry = -1;
  if (lastEntry==entry) return 1;
  lastEntry = entry;
  fgInstance->fEvent->Reset();
  fgInstance->fESDtree->GetEntry(entry);
  if (verbose & 0x1) {
    Int_t nTracks = fgInstance->fEvent->GetNumberOfTracks();
    printf("connect nTracks=%d\n", nTracks);
  }
  fgInstance->fEvent->ConnectTracks();
  return 2;
}


//________________________________________________________________________
Int_t AliESDtools::DumpEventVariables() {
  if (fStreamer== nullptr) {
    ::Error("AliESDtools::DumpEventVariable","Streamer not set");
    return 0;
  }
  Int_t tpcClusterMultiplicity   = fEvent->GetNumberOfTPCClusters();
  const AliMultiplicity *multObj = fEvent->GetMultiplicity();
  Int_t itsNumberOfTracklets   = multObj->GetNumberOfTracklets();

  TVectorF phiCountA(36);
  TVectorF phiCountC(36);
  TVectorF phiCountAITS(36);
  TVectorF phiCountCITS(36);
  TVectorF phiCountAITSOnly(36);
  TVectorF phiCountCITSOnly(36);
  TVectorF tzeroMult(24);  for (Int_t i=1;i<24;i++) tzeroMult[i] = 0.;
  TVectorF vzeroMult(64);  for (Int_t i=1;i<64;i++) vzeroMult[i] = 0.;
  TVectorF itsClustersPerLayer(6); for (Int_t i=1;i<6;i++) itsClustersPerLayer[i] = 0.;
  //
  for (Int_t i=1;i<37;i++){
    phiCountA[i-1] = fHistPhiTPCCounterA->GetBinContent(i);
    phiCountC[i-1] = fHistPhiTPCCounterC->GetBinContent(i);
    phiCountAITS[i-1] = fHistPhiTPCCounterAITS->GetBinContent(i);
    phiCountCITS[i-1] = fHistPhiTPCCounterCITS->GetBinContent(i);
    phiCountAITSOnly[i-1] = fHistPhiITSCounterA->GetBinContent(i);
    phiCountCITSOnly[i-1] = fHistPhiITSCounterC->GetBinContent(i);
  }
  //
  // Additional counters for ITS TPC V0 and T0
  const AliESDTZERO *esdTzero = fEvent->GetESDTZERO();
  const Double32_t *t0amp=esdTzero->GetT0amplitude();
  //

  for (Int_t i=0;i<24;i++) { tzeroMult[i] = t0amp[i]; }
  for (Int_t i=0;i<64;i++) { vzeroMult[i] = fEvent->GetVZEROData()-> GetMultiplicity(i); }
  for (Int_t i=0;i<6;i++)  { itsClustersPerLayer[i] = multObj->GetNumberOfITSClusters(i); }
  Int_t runNumber=fEvent->GetRunNumber();
  Double_t timeStamp= fEvent->GetTimeStampCTPBCCorr();
  Double_t bField=fEvent->GetMagneticField();
  ULong64_t orbitID      = (ULong64_t)fEvent->GetOrbitNumber();
  ULong64_t bunchCrossID = (ULong64_t)fEvent->GetBunchCrossNumber();
  ULong64_t periodID     = (ULong64_t)fEvent->GetPeriodNumber();
  ULong64_t gid = ((periodID << 36) | (orbitID << 12) | bunchCrossID);
  Short_t   fEventMult = fEvent->GetNumberOfTracks();
  /// centrality
  //if (MultSelection) {
  //    if (fUseCouts)  std::cout << " Info::marsland: Centralitity is taken from MultSelection " << std::endl;
  //    fCentrality = MultSelection->GetMultiplicityPercentile("V0M");
  //  } else if (esdCentrality) {
  //    if (fUseCouts)  std::cout << " Info::marsland: Centralitity is taken from esdCentrality " << std::endl;
  //    fCentrality = esdCentrality->GetCentralityPercentile("V0M");
  //  }
  const AliESDVertex *vertex = fEvent->GetPrimaryVertexTracks();
  const AliESDVertex *vertexSPD= fEvent->GetPrimaryVertexTracks();
  const AliESDVertex *vertexTPC= fEvent->GetPrimaryVertexTracks();
  Double_t TPCvZ,fVz, SPDvZ;
  TPCvZ=vertexTPC->GetZ();
  SPDvZ=vertexSPD->GetZ();
  fVz   =vertex->GetZ();
  Int_t primMult    = vertex->GetNContributors();
  Int_t TPCMult = 0;
  Int_t eventMult = fEvent->GetNumberOfESDTracks();
  for (Int_t iTrack=0;iTrack<eventMult;++iTrack){
    AliESDtrack *track = fEvent->GetTrack(iTrack);
    if (track->IsOn(AliESDtrack::kTPCin)) TPCMult++;
  }

  (*fStreamer)<<"events"<<
                     "run="                  << runNumber                 <<  // run Number
                     "bField="               << bField                   <<  // b field
                     "gid="                  << gid              <<  // global event ID
                     "timestamp="            << timeStamp             <<  // timestamp
//                     "centV0M="              << fCentrality            <<  // centrality
//                     "cent.="                << fCentralityEstimates   <<  // track counter
                     "vz="                   << fVz                    <<  // vertex Z
                     "tpcvz="                << TPCvZ                 <<
                     "spdvz="                << SPDvZ                 <<
                     "tpcMult="              << TPCMult               <<  //  TPC multiplicity
                     "eventmult="            << fEventMult             <<  //  event multiplicity
                     "primMult="             << primMult         <<  //  #prim tracks
                     "tpcClusterMult="       << tpcClusterMultiplicity <<  // tpc cluster multiplicity
                     "itsTracklets="         << itsNumberOfTracklets   <<  // number of ITS tracklets
                     //
                     "tzeroMult.="           << &tzeroMult             <<  // T0 multiplicity
                     "vzeroMult.="           << &vzeroMult             <<  // V0 multiplicity
                     "itsClustersPerLayer.=" << &itsClustersPerLayer   <<  // its clusters per layer
                     "trackCounters.="       << fCacheTrackCounters    <<  // track counter
                     "trackdEdxRatio.="      << fCacheTrackdEdxRatio   <<  // dEdx counter
                     "trackNcl.="            << fCacheTrackNcl         <<  // nCluster counter
                     "trackChi2.="           << fCacheTrackChi2        <<  // Chi2 counter
                     "trackMatchEff.="       << fCacheTrackMatchEff    <<  // matching efficiency
                     "trackTPCCountersZ.="   << fCacheTrackTPCCountersZ    <<  // Chi2 counter
//                     "hisTPCVertexA.="       << fHisTPCVertexA         <<  // TPC vertex z
//                     "hisTPCVertexC.="       << fHisTPCVertexC         <<  // TPC vertex z
//                     "hisTPCVertex.="        << fHisTPCVertex          <<  // TPC vertex z
//                     "hisTPCVertexACut.="    << fHisTPCVertexACut      <<  // TPC vertex z
//                     "hisTPCVertexCCut.="    << fHisTPCVertexCCut      <<  // TPC vertex z
//                     "phiTPCdcarA.="         << fPhiTPCdcarA           <<  // track counter
//                     "phiTPCdcarC.="         << fPhiTPCdcarC           <<  // dEdx conter
                     "phiCountA.="           << &phiCountA             <<  // TPC track count on A side
                     "phiCountC.="           << &phiCountC             <<  // TPC track count on C side
                     "phiCountAITS.="        << &phiCountAITS          <<  // track count fitted ITS on A side
                     "phiCountCITS.="        << &phiCountCITS          <<  // track count fitted ITS on C side
                     "phiCountAITSOnly.="    << &phiCountAITSOnly      <<  // track count only ITS on A side
                     "phiCountCITSOnly.="    << &phiCountCITSOnly      <<  // track count only ITS on C side
                     "\n";

  return 0;
}
/// Set default tree alaiases and corresponding metadata for anotation
/// \param tree - input tree
/// \return
Int_t AliESDtools::SetDefaultAliases(TTree* tree) {
  if (!tree) return 0;
  /// FLAGS
  tree->SetAlias("hasTPC", "(Tracks[].fFlags&0x10>0)&&Tracks[].fTPCncls>20");
  tree->SetAlias("hasITS", "(Tracks[].fFlags&0x1>0)&&Tracks[].fITSncls>2");
  tree->SetAlias("hasTRD", "(Tracks[].fFlags&0x100>0)&&Tracks[].fTRDncls/20>2");
  TStatToolkit::AddMetadata(tree, "hasTPC.AxisTitle", "TPC in");
  TStatToolkit::AddMetadata(tree, "hasITS.AxisTitle", "ITS in");
  TStatToolkit::AddMetadata(tree, "hasTRD.AxisTitle", "TRD in");
  /// Track properties
  tree->SetAlias("qP", "sign(esdTrack.fIp.fP[4])/(esdTrack.fIp.P())");
  tree->SetAlias("qPt", "sign(esdTrack.fIp.fP[4])/(esdTrack.fIp.Pt())");
  tree->SetAlias("tgl", "esdTrack.fIp.fP[3]");
  tree->SetAlias("atgl", "abs(esdTrack.fIp.fP[3]+0)");
  tree->SetAlias("mult", "(Tracks@.GetEntries()+0)");
  tree->SetAlias("tgl", "(Tracks[].fP[3]+0)");
  ///dEdx ratios
  tree->SetAlias("mdEdx", "50./Tracks[].fTPCsignal");
  tree->SetAlias("ratioTotMax0", "fTPCdEdxInfo.GetSignalTot(0)/fTPCdEdxInfo.GetSignalMax(0)");
  tree->SetAlias("ratioTotMax1", "fTPCdEdxInfo.GetSignalTot(1)/fTPCdEdxInfo.GetSignalMax(1)");
  tree->SetAlias("ratioTotMax2", "fTPCdEdxInfo.GetSignalTot(2)/fTPCdEdxInfo.GetSignalMax(2)");
  tree->SetAlias("ratioTotMax3", "fTPCdEdxInfo.GetSignalTot(3)/fTPCdEdxInfo.GetSignalMax(3)");
  tree->SetAlias("logRatioTot03", "log(fTPCdEdxInfo.GetSignalTot(0)/fTPCdEdxInfo.GetSignalTot(3))");
  tree->SetAlias("logRatioTot13", "log(fTPCdEdxInfo.GetSignalTot(1)/fTPCdEdxInfo.GetSignalTot(3))");
  tree->SetAlias("logRatioTot23", "log(fTPCdEdxInfo.GetSignalTot(2)/fTPCdEdxInfo.GetSignalTot(3))");
  tree->SetAlias("logRatioMax03", "log(fTPCdEdxInfo.GetSignalMax(0)/fTPCdEdxInfo.GetSignalMax(3))");  //
  tree->SetAlias("logRatioMax13", "log(fTPCdEdxInfo.GetSignalMax(1)/fTPCdEdxInfo.GetSignalMax(3))");  //
  tree->SetAlias("logRatioMax23", "log(fTPCdEdxInfo.GetSignalMax(2)/fTPCdEdxInfo.GetSignalMax(3))");
  tree->SetAlias("logRatioTot01", "log(fTPCdEdxInfo.GetSignalTot(0)/fTPCdEdxInfo.GetSignalTot(1))");
  tree->SetAlias("logRatioTot12", "log(fTPCdEdxInfo.GetSignalTot(1)/fTPCdEdxInfo.GetSignalTot(2))");
  tree->SetAlias("logRatioTot02", "log(fTPCdEdxInfo.GetSignalTot(0)/fTPCdEdxInfo.GetSignalTot(2))");
  tree->SetAlias("logRatioMax01", "log(fTPCdEdxInfo.GetSignalMax(0)/fTPCdEdxInfo.GetSignalMax(1))");  //
  tree->SetAlias("logRatioMax12", "log(fTPCdEdxInfo.GetSignalMax(1)/fTPCdEdxInfo.GetSignalMax(2))");  //
  tree->SetAlias("logRatioMax02", "log(fTPCdEdxInfo.GetSignalMax(0)/fTPCdEdxInfo.GetSignalMax(2))");  //
  /// Faction of clusters and ncrossed rows
  tree->SetAlias("nclFractionROCA", "(Tracks[].GetTPCClusterInfo(3,0)+0)");
  tree->SetAlias("nclFractionROC0", "(Tracks[].GetTPCClusterInfo(3,0,0,63)+0)");
  tree->SetAlias("nclFractionROC1", "(Tracks[].GetTPCClusterInfo(3,0,64,128)+0)");
  tree->SetAlias("nclFractionROC2", "(Tracks[].GetTPCClusterInfo(3,0,129,159)+0)");
  tree->SetAlias("nCross0", "esdTrack.fTPCdEdxInfo.GetNumberOfCrossedRows(0)");
  tree->SetAlias("nCross1", "esdTrack.fTPCdEdxInfo.GetNumberOfCrossedRows(1)");
  tree->SetAlias("nCross2", "esdTrack.fTPCdEdxInfo.GetNumberOfCrossedRows(2)");
  tree->SetAlias("nFraction0", "esdTrack.GetTPCClusterInfo(1,0,0,62)");
  tree->SetAlias("nFraction1", "esdTrack.GetTPCClusterInfo(1,0,63,127)");
  tree->SetAlias("nFraction2", "esdTrack.GetTPCClusterInfo(1,0,127,159)");
  tree->SetAlias("nFraction3", "esdTrack.GetTPCClusterInfo(1,0,0,159)");
  tree->SetAlias("n3Fraction0", "esdTrack.GetTPCClusterInfo(3,0,0,62)");
  tree->SetAlias("n3Fraction1", "esdTrack.GetTPCClusterInfo(3,0,63,127)");
  tree->SetAlias("n3Fraction2", "esdTrack.GetTPCClusterInfo(3,0,127,159)");
  tree->SetAlias("n3Fraction3", "esdTrack.GetTPCClusterInfo(3,0,0,159)");
  //
  for (Int_t i = 0; i < 3; i++) {
    TStatToolkit::AddMetadata(tree, Form("nCross%d.AxisTitle", i), Form("# crossed (ROC%d)", i));
    TStatToolkit::AddMetadata(tree, Form("nclFractionROC%d.AxisTitle", i), Form("fraction of cl (ROC%d)", i));
    TStatToolkit::AddMetadata(tree, Form("nFraction%d.AxisTitle", i), Form("p_{cl1}(ROC%d)", i));
    TStatToolkit::AddMetadata(tree, Form("n3Fraction%d.AxisTitle", i), Form("p_{cl3}(ROC%d)", i));
  }
  for (Int_t i = 0; i < 4; i++)
    TStatToolkit::AddMetadata(tree, Form("ratioTotMax%d.AxisTitle", i), Form("Q_{max%d}/Q_{tot%d}", i,i));
  for (Int_t i = 0; i < 3; i++) {
    for (Int_t j = i + 1; j < 4; j++) {
      TStatToolkit::AddMetadata(tree, Form("logRatioMax%d%d.AxisTitle", i, j), Form("log(Q_{MaxRPC%d}/Q_{MaxROC%d})", i, j));
      TStatToolkit::AddMetadata(tree, Form("logRatioTot%d%d.AxisTitle", i, j), Form("log(Q_{TotRPC%d}/Q_{TotROC%d})", i, j));
    }
  }
  return 1;
}
