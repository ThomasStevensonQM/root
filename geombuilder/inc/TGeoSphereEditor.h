// @(#):$Name:  $:$Id: Exp $
// Author: M.Gheata 
/*************************************************************************
 * Copyright (C) 1995-2002, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TGeoSphereEditor
#define ROOT_TGeoSphereEditor

//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  TGeoSphereEditor                                                      //
//                                                                     //
//  Editor for a TGeoSphere.                                              //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#ifndef ROOT_TGButton
#include "TGWidget.h"
#endif
#ifndef ROOT_TGedFrame
#include "TGedFrame.h"
#endif

class TGeoSphere;
class TGeoTabManager;
class TGTextEntry;
class TGNumberEntry;
class TGTab;
class TGComboBox;
class TGDoubleVSlider;
class TGTextButton;
class TString;

class TGeoSphereEditor : public TGedFrame {

protected:

   Double_t        fRmini;             // Initial inner radius
   Double_t        fRmaxi;             // Initial outer radius
   Double_t        fTheta1i;           // Initial lower theta limit
   Double_t        fTheta2i;           // Initial higher theta limit   
   Double_t        fPhi1i;              // Initial lower phi limit
   Double_t        fPhi2i;              // Initial higher phi limit   
   TString         fNamei;             // Initial name
   TGeoSphere     *fShape;             // Shape object
   Bool_t          fIsModified;        // Flag that volume was modified
   Bool_t          fIsShapeEditable;   // Flag that the shape can be changed
   Bool_t          fLock;              // Lock
   TGeoTabManager *fTabMgr;            // Tab manager
   TGTextEntry    *fShapeName;         // Shape name text entry
   TGNumberEntry  *fERmin;             // Number entry for rmin
   TGNumberEntry  *fERmax;             // Number entry for rmax
   TGNumberEntry  *fETheta1;          // Number entry for Theta1
   TGNumberEntry  *fETheta2;          // Number entry for Theta2
   TGNumberEntry  *fEPhi1;             // Number entry for phi1
   TGNumberEntry  *fEPhi2;             // Number entry for phi2
   TGDoubleVSlider  *fSPhi;           // Phi slider
   TGDoubleVSlider  *fSTheta;          // Theta slider  
   TGTextButton   *fApply;             // Apply-Button to accept changes
   TGTextButton   *fCancel;            // Cancel-Button
   TGTextButton   *fUndo;              // Undo-Button

   virtual void ConnectSignals2Slots();   // Connect the signals to the slots

public:
   TGeoSphereEditor(const TGWindow *p, Int_t id,               
                  Int_t width = 140, Int_t height = 30,
                  UInt_t options = kChildFrame,
                  Pixel_t back = GetDefaultFrameBackground());
   virtual ~TGeoSphereEditor();
   virtual void   SetModel(TVirtualPad *pad, TObject *obj, Int_t event);

   void           DoRmin();
   void           DoRmax();
   void           DoPhi();
   void           DoTheta();
   void           DoTheta1();
   void           DoTheta2();
   void           DoPhi1();
   void           DoPhi2();
   void           DoModified();
   void           DoName();
   virtual void   DoApply();
   virtual void   DoCancel();
   virtual void   DoUndo();
   
   ClassDef(TGeoSphereEditor,0)   // TGeoSphere editor
};   

#endif                    
