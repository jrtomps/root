// @(#)root/histpainter:$Id: TGraphPainter.cxx,v 1.00
// Author: Olivier Couet

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#include "TROOT.h"
#include "TGraphPainter.h"
#include "TMath.h"
#include "TGraph.h"
#include "TPolyLine.h"
#include "TPolyMarker.h"
#include "TVirtualPad.h"
#include "TView.h"
#include "TStyle.h"
#include "TH1.h"
#include "TF1.h"
#include "TPaveStats.h"
#include "TGaxis.h"
#include "TArrow.h"
#include "TFrame.h"

Double_t *gxwork, *gywork, *gxworkl, *gyworkl;

ClassImp(TGraphPainter)


//______________________________________________________________________________
//
// TGraphPainter paints TGraph
//


//______________________________________________________________________________
TGraphPainter::TGraphPainter()
{
   // TGraphPainter default constructor

}


//______________________________________________________________________________
TGraphPainter::~TGraphPainter()
{
   // TGraphPainter destructor.
}


//______________________________________________________________________________
void TGraphPainter::ComputeLogs(Int_t npoints, Int_t opt)
{   
   // Convert WC from Log scales.
   //
   //   Take the LOG10 of gxwork and gywork according to the value of Options
   //   and put it in gxworkl and gyworkl.
   //
   //  npoints : Number of points in gxwork and in gywork.
   //
			      
   Int_t i;
   memcpy(gxworkl,gxwork,npoints*8);
   memcpy(gyworkl,gywork,npoints*8);
   if (gPad->GetLogx()) {
      for (i=0;i<npoints;i++) {
         if (gxworkl[i] > 0) gxworkl[i] = TMath::Log10(gxworkl[i]);
         else                gxworkl[i] = gPad->GetX1();
      }
   }
   if (!opt && gPad->GetLogy()) {
      for (i=0;i<npoints;i++) {
         if (gyworkl[i] > 0) gyworkl[i] = TMath::Log10(gyworkl[i]);
         else                gyworkl[i] = gPad->GetY1();
      }
   }
}


//______________________________________________________________________________
Int_t TGraphPainter::DistancetoPrimitiveHelper(TGraph *theGraph, Int_t px, Int_t py)
{
   // Compute distance from point px,py to a graph.
   //
   //  Compute the closest distance of approach from point px,py to this line.
   //  The distance is computed in pixels units.

   // Are we on the axis?
   Int_t distance;
   if (theGraph->GetHistogram()) {
      distance = theGraph->GetHistogram()->DistancetoPrimitive(px,py);
      if (distance <= 5) return distance;
   }

   // Somewhere on the graph points?
   const Int_t big = 9999;
   const Int_t kMaxDiff = 10;
   Int_t puxmin = gPad->XtoAbsPixel(gPad->GetUxmin());
   Int_t puymin = gPad->YtoAbsPixel(gPad->GetUymin());
   Int_t puxmax = gPad->XtoAbsPixel(gPad->GetUxmax());
   Int_t puymax = gPad->YtoAbsPixel(gPad->GetUymax());

   // return if point is not in the graph area
   if (px <= puxmin) return big;
   if (py >= puymin) return big;
   if (px >= puxmax) return big;
   if (py <= puymax) return big;

   // check if point is near one of the graph points
   Int_t i, pxp, pyp, d;
   distance = big;

   Int_t theNpoints = theGraph->GetN();
   Double_t *theX  = theGraph->GetX();
   Double_t *theY  = theGraph->GetY();
   
   for (i=0;i<theNpoints;i++) {
      pxp = gPad->XtoAbsPixel(gPad->XtoPad(theX[i]));
      pyp = gPad->YtoAbsPixel(gPad->YtoPad(theY[i]));
      d   = TMath::Abs(pxp-px) + TMath::Abs(pyp-py);
      if (d < distance) distance = d;
   }
   if (distance < kMaxDiff) return distance;

   for (i=0;i<theNpoints-1;i++) {
      TAttLine l;
      d = l.DistancetoLine(px, py, gPad->XtoPad(theX[i]), gPad->YtoPad(theY[i]), gPad->XtoPad(theX[i+1]), gPad->YtoPad(theY[i+1]));
      if (d < distance) distance = d;
   }

   // If graph has been drawn with the fill area option, check if we are inside
   TString drawOption = GetDrawOption();
   drawOption.ToLower();
   if (drawOption.Contains("f")) {
      Double_t xp = gPad->AbsPixeltoX(px); xp = gPad->PadtoX(xp);
      Double_t yp = gPad->AbsPixeltoY(py); yp = gPad->PadtoY(yp);
      if (TMath::IsInside(xp,yp,theNpoints,theX,theY) != 0) distance = 1;
   }

   // Loop on the list of associated functions and user objects
   TObject *f;
   TList *functions = theGraph->GetListOfFunctions();
   TIter   next(functions);
   while ((f = (TObject*) next())) {
      Int_t dist;
      if (f->InheritsFrom(TF1::Class())) dist = f->DistancetoPrimitive(-px,py);
      else                               dist = f->DistancetoPrimitive(px,py);
      if (dist < kMaxDiff) {
         gPad->SetSelected(f);
         return 0; //must be o and not dist in case of TMultiGraph
      }
   }

   return distance;
}


//______________________________________________________________________________
void TGraphPainter::ExecuteEventHelper(TGraph *theGraph, Int_t event, Int_t px, Int_t py)
{
   // Execute action corresponding to one event.
   //
   //  This member function is called when a graph is clicked with the locator
   //
   //  If Left button clicked on one of the line end points, this point
   //     follows the cursor until button is released.
   //
   //  if Middle button clicked, the line is moved parallel to itself
   //     until the button is released.

   Int_t i, d;
   Double_t xmin, xmax, ymin, ymax, dx, dy, dxr, dyr;
   const Int_t kMaxDiff = 10;
   static Bool_t middle, badcase;
   static Int_t ipoint, pxp, pyp;
   static Int_t px1,px2,py1,py2;
   static Int_t pxold, pyold, px1old, py1old, px2old, py2old;
   static Int_t dpx, dpy;
   static Int_t *x=0, *y=0;

   if (!theGraph->IsEditable()) {gPad->SetCursor(kHand); return;}
   if (!gPad->IsEditable()) return;
   Int_t theNpoints = theGraph->GetN();
   Double_t *theX  = theGraph->GetX();
   Double_t *theY  = theGraph->GetY();
 
   switch (event) {

   case kButton1Down:
      badcase = kFALSE;
      gVirtualX->SetLineColor(-1);
      theGraph->TAttLine::Modify();  //Change line attributes only if necessary
      px1 = gPad->XtoAbsPixel(gPad->GetX1());
      py1 = gPad->YtoAbsPixel(gPad->GetY1());
      px2 = gPad->XtoAbsPixel(gPad->GetX2());
      py2 = gPad->YtoAbsPixel(gPad->GetY2());
      ipoint = -1;


      if (x || y) break;
      x = new Int_t[theNpoints+1];
      y = new Int_t[theNpoints+1];
      for (i=0;i<theNpoints;i++) {
         pxp = gPad->XtoAbsPixel(gPad->XtoPad(theX[i]));
         pyp = gPad->YtoAbsPixel(gPad->YtoPad(theY[i]));
         if (pxp < -kMaxPixel || pxp >= kMaxPixel ||
             pyp < -kMaxPixel || pyp >= kMaxPixel) {
            badcase = kTRUE;
            continue;
         }
         gVirtualX->DrawLine(pxp-4, pyp-4, pxp+4,  pyp-4);
         gVirtualX->DrawLine(pxp+4, pyp-4, pxp+4,  pyp+4);
         gVirtualX->DrawLine(pxp+4, pyp+4, pxp-4,  pyp+4);
         gVirtualX->DrawLine(pxp-4, pyp+4, pxp-4,  pyp-4);
         x[i] = pxp;
         y[i] = pyp;
         d   = TMath::Abs(pxp-px) + TMath::Abs(pyp-py);
         if (d < kMaxDiff) ipoint =i;
      }
      dpx = 0;
      dpy = 0;
      pxold = px;
      pyold = py;
      if (ipoint < 0) return;
      if (ipoint == 0) {
         px1old = 0;
         py1old = 0;
         px2old = gPad->XtoAbsPixel(theX[1]);
         py2old = gPad->YtoAbsPixel(theY[1]);
      } else if (ipoint == theNpoints-1) {
         px1old = gPad->XtoAbsPixel(gPad->XtoPad(theX[theNpoints-2]));
         py1old = gPad->YtoAbsPixel(gPad->YtoPad(theY[theNpoints-2]));
         px2old = 0;
         py2old = 0;
      } else {
         px1old = gPad->XtoAbsPixel(gPad->XtoPad(theX[ipoint-1]));
         py1old = gPad->YtoAbsPixel(gPad->YtoPad(theY[ipoint-1]));
         px2old = gPad->XtoAbsPixel(gPad->XtoPad(theX[ipoint+1]));
         py2old = gPad->YtoAbsPixel(gPad->YtoPad(theY[ipoint+1]));
      }
      pxold = gPad->XtoAbsPixel(gPad->XtoPad(theX[ipoint]));
      pyold = gPad->YtoAbsPixel(gPad->YtoPad(theY[ipoint]));

      break;


   case kMouseMotion:

      middle = kTRUE;
      for (i=0;i<theNpoints;i++) {
         pxp = gPad->XtoAbsPixel(gPad->XtoPad(theX[i]));
         pyp = gPad->YtoAbsPixel(gPad->YtoPad(theY[i]));
         d   = TMath::Abs(pxp-px) + TMath::Abs(pyp-py);
         if (d < kMaxDiff) middle = kFALSE;
      }


   // check if point is close to an axis
      if (middle) gPad->SetCursor(kMove);
      else gPad->SetCursor(kHand);
      break;

   case kButton1Motion:
      if (middle) {
         for(i=0;i<theNpoints-1;i++) {
            gVirtualX->DrawLine(x[i]+dpx, y[i]+dpy, x[i+1]+dpx, y[i+1]+dpy);
            pxp = x[i]+dpx;
            pyp = y[i]+dpy;
            if (pxp < -kMaxPixel || pxp >= kMaxPixel ||
                pyp < -kMaxPixel || pyp >= kMaxPixel) continue;
            gVirtualX->DrawLine(pxp-4, pyp-4, pxp+4,  pyp-4);
            gVirtualX->DrawLine(pxp+4, pyp-4, pxp+4,  pyp+4);
            gVirtualX->DrawLine(pxp+4, pyp+4, pxp-4,  pyp+4);
            gVirtualX->DrawLine(pxp-4, pyp+4, pxp-4,  pyp-4);
         }
         pxp = x[theNpoints-1]+dpx;
         pyp = y[theNpoints-1]+dpy;
         gVirtualX->DrawLine(pxp-4, pyp-4, pxp+4,  pyp-4);
         gVirtualX->DrawLine(pxp+4, pyp-4, pxp+4,  pyp+4);
         gVirtualX->DrawLine(pxp+4, pyp+4, pxp-4,  pyp+4);
         gVirtualX->DrawLine(pxp-4, pyp+4, pxp-4,  pyp-4);
         dpx += px - pxold;
         dpy += py - pyold;
         pxold = px;
         pyold = py;
         for(i=0;i<theNpoints-1;i++) {
            gVirtualX->DrawLine(x[i]+dpx, y[i]+dpy, x[i+1]+dpx, y[i+1]+dpy);
            pxp = x[i]+dpx;
            pyp = y[i]+dpy;
            if (pxp < -kMaxPixel || pxp >= kMaxPixel ||
                pyp < -kMaxPixel || pyp >= kMaxPixel) continue;
            gVirtualX->DrawLine(pxp-4, pyp-4, pxp+4,  pyp-4);
            gVirtualX->DrawLine(pxp+4, pyp-4, pxp+4,  pyp+4);
            gVirtualX->DrawLine(pxp+4, pyp+4, pxp-4,  pyp+4);
            gVirtualX->DrawLine(pxp-4, pyp+4, pxp-4,  pyp-4);
         }
         pxp = x[theNpoints-1]+dpx;
         pyp = y[theNpoints-1]+dpy;
         gVirtualX->DrawLine(pxp-4, pyp-4, pxp+4,  pyp-4);
         gVirtualX->DrawLine(pxp+4, pyp-4, pxp+4,  pyp+4);
         gVirtualX->DrawLine(pxp+4, pyp+4, pxp-4,  pyp+4);
         gVirtualX->DrawLine(pxp-4, pyp+4, pxp-4,  pyp-4);
      } else {
         if (px1old) gVirtualX->DrawLine(px1old, py1old, pxold,  pyold);
         if (px2old) gVirtualX->DrawLine(pxold,  pyold,  px2old, py2old);
         gVirtualX->DrawLine(pxold-4, pyold-4, pxold+4,  pyold-4);
         gVirtualX->DrawLine(pxold+4, pyold-4, pxold+4,  pyold+4);
         gVirtualX->DrawLine(pxold+4, pyold+4, pxold-4,  pyold+4);
         gVirtualX->DrawLine(pxold-4, pyold+4, pxold-4,  pyold-4);
         pxold = px;
         pxold = TMath::Max(pxold, px1);
         pxold = TMath::Min(pxold, px2);
         pyold = py;
         pyold = TMath::Max(pyold, py2);
         pyold = TMath::Min(pyold, py1);
         if (px1old) gVirtualX->DrawLine(px1old, py1old, pxold,  pyold);
         if (px2old) gVirtualX->DrawLine(pxold,  pyold,  px2old, py2old);
         gVirtualX->DrawLine(pxold-4, pyold-4, pxold+4,  pyold-4);
         gVirtualX->DrawLine(pxold+4, pyold-4, pxold+4,  pyold+4);
         gVirtualX->DrawLine(pxold+4, pyold+4, pxold-4,  pyold+4);
         gVirtualX->DrawLine(pxold-4, pyold+4, pxold-4,  pyold-4);
      }
      break;

   case kButton1Up:

      if (gROOT->IsEscaped()) {
         gROOT->SetEscape(kFALSE);
         delete [] x; x = 0;
         delete [] y; y = 0;
         break;
      }

   // Compute x,y range
      xmin = gPad->GetUxmin();
      xmax = gPad->GetUxmax();
      ymin = gPad->GetUymin();
      ymax = gPad->GetUymax();
      dx   = xmax-xmin;
      dy   = ymax-ymin;
      dxr  = dx/(1 - gPad->GetLeftMargin() - gPad->GetRightMargin());
      dyr  = dy/(1 - gPad->GetBottomMargin() - gPad->GetTopMargin());

      if (theGraph->GetHistogram()) {
         // Range() could change the size of the pad pixmap and therefore should
         // be called before the other paint routines
         gPad->Range(xmin - dxr*gPad->GetLeftMargin(),
                      ymin - dyr*gPad->GetBottomMargin(),
                      xmax + dxr*gPad->GetRightMargin(),
                      ymax + dyr*gPad->GetTopMargin());
         gPad->RangeAxis(xmin, ymin, xmax, ymax);
      }
      if (middle) {
         for(i=0;i<theNpoints;i++) {
            if (badcase) continue;  //do not update if big zoom and points moved
            if (x) theX[i] = gPad->PadtoX(gPad->AbsPixeltoX(x[i]+dpx));
            if (y) theY[i] = gPad->PadtoY(gPad->AbsPixeltoY(y[i]+dpy));
         }
      } else {
         theX[ipoint] = gPad->PadtoX(gPad->AbsPixeltoX(pxold));
         theY[ipoint] = gPad->PadtoY(gPad->AbsPixeltoY(pyold));
         if (InheritsFrom("TCutG")) {
            //make sure first and last point are the same
            if (ipoint == 0) {
               theX[theNpoints-1] = theX[0];
               theY[theNpoints-1] = theY[0];
            }
            if (ipoint == theNpoints-1) {
               theX[0] = theX[theNpoints-1];
               theY[0] = theY[theNpoints-1];
            }
         }
      }
      badcase = kFALSE;
      delete [] x; x = 0;
      delete [] y; y = 0;
      gPad->Modified(kTRUE);
      gVirtualX->SetLineColor(-1);
   }
}


//______________________________________________________________________________
char *TGraphPainter::GetObjectInfoHelper(TGraph * /*theGraph*/, Int_t /*px*/, Int_t /*py*/) const
{
   return "";
}


//______________________________________________________________________________
void TGraphPainter::PaintHelper(TGraph *theGraph, Option_t *option)
{
   // Paint a any kind of TGraph

   if (theGraph) {
      SetBit(TGraph::kClipFrame, theGraph->TestBit(TGraph::kClipFrame));
      if (theGraph->InheritsFrom("TGraphBentErrors")) {
         PaintGraphBentErrors(theGraph,option);
      } else if (theGraph->InheritsFrom("TGraphAsymmErrors")) {
         PaintGraphAsymmErrors(theGraph,option);
      } else if (theGraph->InheritsFrom("TGraphErrors")) {
         PaintGraphErrors(theGraph,option);
      } else {
         PaintGraphSimple(theGraph,option);
      }
   }
}


//______________________________________________________________________________
void TGraphPainter::PaintGraph(TGraph *theGraph, Int_t npoints, const Double_t *x, const Double_t *y, Option_t *chopt)
{
   // Control function to draw a graph.
   //
   //  Draws one dimensional graphs. The aspect of the graph is done
   //  according to the value of the chopt.
   //
   //  Input parameters:
   //
   //  npoints : Number of points in X or in Y.
   //  x[npoints] or x[2] : X coordinates or (XMIN,XMAX) (WC space).
   //  y[npoints] or y[2] : Y coordinates or (YMIN,YMAX) (WC space).
   //  chopt : Option.
   //
   //  chopt='L' :  A simple polyline between every points is drawn
   //
   //  chopt='F' :  A fill area is drawn ('CF' draw a smooth fill area)
   //
   //  chopt='A' :  Axis are drawn around the graph
   //
   //  chopt='C' :  A smooth Curve is drawn
   //
   //  chopt='*' :  A Star is plotted at each point
   //
   //  chopt='P' :  Idem with the current marker
   //
   //  chopt='B' :  A Bar chart is drawn at each point
   //
   //  chopt='1' :  ylow=rwymin
   //
   //  chopt='X+' : The X-axis is drawn on the top side of the plot.
   //
   //  chopt='Y+' : The Y-axis is drawn on the right side of the plot.
   //
   // When a graph is painted with the option "C" or "L" it is possible to draw
   // a filled area on one side of the line. This is useful to show exclusion
   // zones. This drawing mode is activated when the absolute value of the
   // graph line width (set thanks to SetLineWidth) is greater than 99. In that
   // case the line width number is interpreted as 100*ff+ll = ffll . The two
   // digits number "ll" represent the normal line width whereas "ff" is the
   // filled area width. The sign of "ffll" allows to flip the filled area
   // from one side of the line to the other. The current fill area attributes
   // are used to draw the hatched zone.


   Int_t optionLine , optionAxis , optionCurve, optionStar , optionMark;
   Int_t optionBar  , optionR    , optionOne  , optionE;
   Int_t optionFill , optionZ    , optionCurveFill;
   Int_t i, npt, nloop;
   Int_t drawtype=0;
   Double_t xlow, xhigh, ylow, yhigh;
   Double_t barxmin, barxmax, barymin, barymax;
   Double_t uxmin, uxmax;
   Double_t x1, xn, y1, yn;
   Double_t dbar, bdelta;
   Int_t theNpoints = theGraph->GetN();
   
   if (npoints <= 0) {
      Error("PaintGraph", "illegal number of points (%d)", npoints);
      return;
   }
   TString opt = chopt;
   opt.ToUpper();
   opt.ReplaceAll("SAME","");

   if(opt.Contains("L")) optionLine = 1;  else optionLine = 0;
   if(opt.Contains("A")) optionAxis = 1;  else optionAxis = 0;
   if(opt.Contains("C")) optionCurve= 1;  else optionCurve= 0;
   if(opt.Contains("*")) optionStar = 1;  else optionStar = 0;
   if(opt.Contains("P")) optionMark = 1;  else optionMark = 0;
   if(opt.Contains("B")) optionBar  = 1;  else optionBar  = 0;
   if(opt.Contains("R")) optionR    = 1;  else optionR    = 0;
   if(opt.Contains("1")) optionOne  = 1;  else optionOne  = 0;
   if(opt.Contains("F")) optionFill = 1;  else optionFill = 0;
   if(opt.Contains("2") || opt.Contains("3") ||
      opt.Contains("4")) optionE = 1;  else optionE = 0;
   optionZ    = 0;

   // If no "drawing" option is selected and if chopt<>' ' nothing is done.
   if (optionLine+optionFill+optionCurve+optionStar+optionMark+optionBar+optionE == 0) {
      if (strlen(chopt) == 0)  optionLine=1;
      else   return;
   }

   if (optionStar) theGraph->SetMarkerStyle(3);

   optionCurveFill = 0;
   if (optionCurve && optionFill) {
      optionCurveFill = 1;
      optionFill      = 0;
   }

   // Draw the Axis.
   Double_t rwxmin,rwxmax, rwymin, rwymax, maximum, minimum, dx, dy;
   if (optionAxis) {
      if (theGraph->GetHistogram()) {
         rwxmin    = gPad->GetUxmin();
         rwxmax    = gPad->GetUxmax();
         rwymin    = gPad->GetUymin();
         rwymax    = gPad->GetUymax();
         minimum   = theGraph->GetHistogram()->GetMinimumStored();
         maximum   = theGraph->GetHistogram()->GetMaximumStored();
         if (minimum == -1111) { //this can happen after unzooming
            minimum = theGraph->GetHistogram()->GetYaxis()->GetXmin();
            theGraph->GetHistogram()->SetMinimum(minimum);
         }
         if (maximum == -1111) {
            maximum = theGraph->GetHistogram()->GetYaxis()->GetXmax();
            theGraph->GetHistogram()->SetMaximum(maximum);
         }
         uxmin     = gPad->PadtoX(rwxmin);
         uxmax     = gPad->PadtoX(rwxmax);
      } else {

         theGraph->ComputeRange(rwxmin, rwymin, rwxmax, rwymax);  //this is redefined in TGraphErrors

         if (rwxmin == rwxmax) rwxmax += 1.;
         if (rwymin == rwymax) rwymax += 1.;
         dx = 0.1*(rwxmax-rwxmin);
         dy = 0.1*(rwymax-rwymin);
         uxmin    = rwxmin - dx;
         uxmax    = rwxmax + dx;
         minimum  = rwymin - dy;
         maximum  = rwymax + dy;
      }
      if (theGraph->GetMinimum() != -1111) rwymin = minimum = theGraph->GetMinimum();
      if (theGraph->GetMaximum() != -1111) rwymax = maximum = theGraph->GetMaximum();
      if (uxmin < 0 && rwxmin >= 0) uxmin = 0.9*rwxmin;
      if (uxmax > 0 && rwxmax <= 0) {
         if (gPad->GetLogx()) uxmax = 1.1*rwxmax;
         else                 uxmax = 0;
      }
      if (minimum < 0 && rwymin >= 0) minimum = 0.9*rwymin;
      if (maximum > 0 && rwymax <= 0) {
         //if(gPad->GetLogy()) maximum = 1.1*rwymax;
         //else                maximum = 0;
      }
      if (minimum <= 0 && gPad->GetLogy()) minimum = 0.001*maximum;
      if (uxmin <= 0 && gPad->GetLogx()) {
         if (uxmax > 1000) uxmin = 1;
         else              uxmin = 0.001*uxmax;
      }
      rwymin = minimum;
      rwymax = maximum;

      // Create a temporary histogram and fill each channel with the
      // function value.
      char chopth[8] = " ";
      if (strstr(chopt,"x+")) strcat(chopth, "x+");
      if (strstr(chopt,"y+")) strcat(chopth, "y+");
      if (!theGraph->GetHistogram()) {
         // the graph is created with at least as many channels as there are
         // points to permit zooming on the full range.
         rwxmin = uxmin;
         rwxmax = uxmax;
         npt = 100;
         if (theNpoints > npt) npt = theNpoints;
         TH1 *h = new TH1F(Form("%s_h",GetName()),GetTitle(),npt,rwxmin,rwxmax);
         theGraph->SetHistogram(h);
         if (!theGraph->GetHistogram()) return;
         theGraph->GetHistogram()->SetMinimum(rwymin);
         theGraph->GetHistogram()->SetMaximum(rwymax);
         theGraph->GetHistogram()->GetYaxis()->SetLimits(rwymin,rwymax);
         theGraph->GetHistogram()->SetBit(TH1::kNoStats);
         theGraph->GetHistogram()->SetDirectory(0);
         theGraph->GetHistogram()->Paint(chopth); // Draw histogram axis, title and grid
      } else {
         if (gPad->GetLogy()) {
            theGraph->GetHistogram()->SetMinimum(rwymin);
            theGraph->GetHistogram()->SetMaximum(rwymax);
            theGraph->GetHistogram()->GetYaxis()->SetLimits(rwymin,rwymax);
         }
         theGraph->GetHistogram()->Paint(chopth); // Draw histogram axis, title and grid
      }
   }

   // Set Clipping option
   gPad->SetBit(TGraph::kClipFrame, TestBit(TGraph::kClipFrame));

   TF1 *fit = 0;
   TList *functions = theGraph->GetListOfFunctions();
   if (functions) fit = (TF1*)functions->First();
   TObject *f;
   if (functions) {
      TIter   next(functions);
      while ((f = (TObject*) next())) {
         if (f->InheritsFrom(TF1::Class())) {
            fit = (TF1*)f;
            break;
         }
      }
   }
   if (fit) PaintStats(theGraph, fit);

   rwxmin   = gPad->GetUxmin();
   rwxmax   = gPad->GetUxmax();
   rwymin   = gPad->GetUymin();
   rwymax   = gPad->GetUymax();
   uxmin    = gPad->PadtoX(rwxmin);
   uxmax    = gPad->PadtoX(rwxmax);
   if (theGraph->GetHistogram()) {
      maximum = theGraph->GetHistogram()->GetMaximum();
      minimum = theGraph->GetHistogram()->GetMinimum();
   } else {
      maximum = gPad->PadtoY(rwymax);
      minimum = gPad->PadtoY(rwymin);
   }

   // Set attributes
   theGraph->TAttLine::Modify();
   theGraph->TAttFill::Modify();
   theGraph->TAttMarker::Modify();

   // Draw the graph with a polyline or a fill area
   gxwork  = new Double_t[2*npoints+10];
   gywork  = new Double_t[2*npoints+10];
   gxworkl = new Double_t[2*npoints+10];
   gyworkl = new Double_t[2*npoints+10];

   if (optionLine || optionFill) {
      x1    = x[0];
      xn    = x[npoints-1];
      y1    = y[0];
      yn    = y[npoints-1];
      nloop = npoints;
      if (optionFill && (xn != x1 || yn != y1)) nloop++;
      npt = 0;
      for (i=1;i<=nloop;i++) {
         if (i > npoints) {
            gxwork[npt] = gxwork[0];  gywork[npt] = gywork[0];
         } else {
            gxwork[npt] = x[i-1];      gywork[npt] = y[i-1];
            npt++;
         }
         if (i == nloop) {
            ComputeLogs(npt, optionZ);
            Int_t bord = gStyle->GetDrawBorder();
            if (optionR) {
               if (optionFill) {
                  gPad->PaintFillArea(npt,gyworkl,gxworkl);
                  if (bord) gPad->PaintPolyLine(npt,gyworkl,gxworkl);
               } else {
                  if (TMath::Abs(theGraph->GetLineWidth())>99) PaintPolyLineHatches(theGraph, npt, gyworkl, gxworkl);
                  gPad->PaintPolyLine(npt,gyworkl,gxworkl);
               }
            }
            else {
               if (optionFill) {
                  gPad->PaintFillArea(npt,gxworkl,gyworkl);
                  if (bord) gPad->PaintPolyLine(npt,gxworkl,gyworkl);
               } else {
                  if (TMath::Abs(theGraph->GetLineWidth())>99) PaintPolyLineHatches(theGraph, npt, gxworkl, gyworkl);
                  gPad->PaintPolyLine(npt,gxworkl,gyworkl);
               }
            }
            gxwork[0] = gxwork[npt-1];  gywork[0] = gywork[npt-1];
            npt      = 1;
         }
      }
   }

   // Draw the graph with a smooth Curve. Smoothing via Smooth
   if (optionCurve) {
      x1 = x[0];
      xn = x[npoints-1];
      y1 = y[0];
      yn = y[npoints-1];
      drawtype = 1;
      nloop = npoints;
      if (optionCurveFill) {
         drawtype += 1000;
         if (xn != x1 || yn != y1) nloop++;
      }
      if (!optionR) {
         npt = 0;
         for (i=1;i<=nloop;i++) {
            if (i > npoints) {
               gxwork[npt] = gxwork[0];  gywork[npt] = gywork[0];
            } else {
               gxwork[npt] = x[i-1];      gywork[npt] = y[i-1];
               npt++;
            }
            ComputeLogs(npt, optionZ);
            if (gyworkl[npt-1] < rwymin || gyworkl[npt-1] > rwymax) {
               if (npt > 2) {
                  ComputeLogs(npt, optionZ);
                  Smooth(theGraph, npt,gxworkl,gyworkl,drawtype);
               }
               gxwork[0] = gxwork[npt-1]; gywork[0] = gywork[npt-1];
               npt=1;
               continue;
            }
         }
         if (npt > 1) {
            ComputeLogs(npt, optionZ);
            Smooth(theGraph, npt,gxworkl,gyworkl,drawtype);
         }
      }
      else {
         drawtype += 10;
         npt    = 0;
         for (i=1;i<=nloop;i++) {
            if (i > npoints) {
               gxwork[npt] = gxwork[0];  gywork[npt] = gywork[0];
            } else {
               if (y[i-1] < minimum || y[i-1] > maximum) continue;
               if (x[i-1] < uxmin    || x[i-1] > uxmax)  continue;
               gxwork[npt] = x[i-1];      gywork[npt] = y[i-1];
               npt++;
            }
            ComputeLogs(npt, optionZ);
            if (gxworkl[npt-1] < rwxmin || gxworkl[npt-1] > rwxmax) {
               if (npt > 2) {
                  ComputeLogs(npt, optionZ);
                  Smooth(theGraph, npt,gxworkl,gyworkl,drawtype);
               }
               gxwork[0] = gxwork[npt-1]; gywork[0] = gywork[npt-1];
               npt=1;
               continue;
            }
         }
         if (npt > 1) {
            ComputeLogs(npt, optionZ);
            Smooth(theGraph, npt,gxworkl,gyworkl,drawtype);
         }
      }
   }

   // Draw the graph with a '*' on every points
   if (optionStar) {
      theGraph->SetMarkerStyle(3);
      npt = 0;
      for (i=1;i<=npoints;i++) {
         if (y[i-1] >= minimum && y[i-1] <= maximum && x[i-1] >= uxmin  && x[i-1] <= uxmax) {
            gxwork[npt] = x[i-1];      gywork[npt] = y[i-1];
            npt++;
         }
         if (i == npoints) {
            ComputeLogs(npt, optionZ);
            if (optionR)  gPad->PaintPolyMarker(npt,gyworkl,gxworkl);
            else          gPad->PaintPolyMarker(npt,gxworkl,gyworkl);
            npt = 0;
         }
      }
   }

   // Draw the graph with the current polymarker on every points
   if (optionMark) {
      npt = 0;
      for (i=1;i<=npoints;i++) {
         if (y[i-1] >= minimum && y[i-1] <= maximum && x[i-1] >= uxmin  && x[i-1] <= uxmax) {
            gxwork[npt] = x[i-1];      gywork[npt] = y[i-1];
            npt++;
         }
         if (i == npoints) {
            ComputeLogs(npt, optionZ);
            if (optionR) gPad->PaintPolyMarker(npt,gyworkl,gxworkl);
            else         gPad->PaintPolyMarker(npt,gxworkl,gyworkl);
            npt = 0;
         }
      }
   }

   // Draw the graph as a bar chart
   if (optionBar) {
      if (!optionR) {
         barxmin = x[0];
         barxmax = x[0];
         for (i=1;i<npoints;i++) {
            if (x[i] < barxmin) barxmin = x[i];
            if (x[i] > barxmax) barxmax = x[i];
         }
         bdelta = (barxmax-barxmin)/Double_t(npoints);
      }
      else {
         barymin = y[0];
         barymax = y[0];
         for (i=1;i<npoints;i++) {
            if (y[i] < barymin) barymin = y[i];
            if (y[i] > barymax) barymax = y[i];
         }
         bdelta = (barymax-barymin)/Double_t(npoints);
      }
      dbar  = 0.5*bdelta*gStyle->GetBarWidth();
      if (!optionR) {
         for (i=1;i<=npoints;i++) {
            xlow  = x[i-1] - dbar;
            xhigh = x[i-1] + dbar;
            yhigh = y[i-1];
            if (xlow  < uxmin) continue;
            if (xhigh > uxmax) continue;
            if (!optionOne) ylow = TMath::Max((Double_t)0,gPad->GetUymin());
            else            ylow = gPad->GetUymin();
            gxwork[0] = xlow;
            gywork[0] = ylow;
            gxwork[1] = xhigh;
            gywork[1] = yhigh;
            ComputeLogs(2, optionZ);
            if (gyworkl[0] < gPad->GetUymin()) gyworkl[0] = gPad->GetUymin();
            if (gyworkl[1] < gPad->GetUymin()) continue;
            if (gyworkl[1] > gPad->GetUymax()) gyworkl[1] = gPad->GetUymax();
            if (gyworkl[0] > gPad->GetUymax()) continue;

            gPad->PaintBox(gxworkl[0],gyworkl[0],gxworkl[1],gyworkl[1]);
         }
      }
      else {
         for (i=1;i<=npoints;i++) {
            xhigh = x[i-1];
            ylow  = y[i-1] - dbar;
            yhigh = y[i-1] + dbar;
            xlow     = TMath::Max((Double_t)0, gPad->GetUxmin());
            gxwork[0] = xlow;
            gywork[0] = ylow;
            gxwork[1] = xhigh;
            gywork[1] = yhigh;
            ComputeLogs(2, optionZ);
            gPad->PaintBox(gxworkl[0],gyworkl[0],gxworkl[1],gyworkl[1]);
         }
      }
   }
   gPad->ResetBit(TGraph::kClipFrame);

   delete [] gxwork;
   delete [] gywork;
   delete [] gxworkl;
   delete [] gyworkl;
}


//______________________________________________________________________________
void TGraphPainter::PaintGrapHist(TGraph *theGraph, Int_t npoints, const Double_t *x, const Double_t *y, Option_t *chopt)
{
   // Control function to draw a graphistogram.
   //
   //   Draws one dimensional graphs. The aspect of the graph is done
   // according to the value of the chopt.
   //
   // Input parameters:
   //
   //  npoints : Number of points in X or in Y.
   //  X(N) or x[1] : X coordinates or (XMIN,XMAX) (WC space).
   //  Y(N) or y[1] : Y coordinates or (YMIN,YMAX) (WC space).
   //  chopt : Option.
   //
   //  chopt='R' :  Graph is drawn horizontaly, parallel to X axis.
   //               (default is vertically, parallel to Y axis)
   //               If option R is selected the user must give:
   //                 2 values for Y (y[0]=YMIN and y[1]=YMAX)
   //                 N values for X, one for each channel.
   //               Otherwise the user must give:
   //                 N values for Y, one for each channel.
   //                 2 values for X (x[0]=XMIN and x[1]=XMAX)
   //
   //  chopt='L' :  A simple polyline beetwen every points is drawn
   //
   //  chopt='H' :  An Histogram with equidistant bins is drawn
   //               as a polyline.
   //
   //  chopt='F' :  An histogram with equidistant bins is drawn
   //               as a fill area. Contour is not drawn unless
   //               chopt='H' is also selected..
   //
   //  chopt='N' :  Non equidistant bins (default is equidistant)
   //               If N is the number of channels array X and Y
   //               must be dimensionned as follow:
   //               If option R is not selected (default) then
   //               the user must give:
   //                 (N+1) values for X (limits of channels).
   //                  N values for Y, one for each channel.
   //               Otherwise the user must give:
   //                 (N+1) values for Y (limits of channels).
   //                  N values for X, one for each channel.
   //
   //  chopt='F1':  Idem as 'F' except that fill area is no more
   //               reparted arround axis X=0 or Y=0 .
   //
   //  chopt='F2':  Draw a Fill area polyline connecting the center of bins
   //
   //  chopt='C' :  A smooth Curve is drawn.
   //
   //  chopt='*' :  A Star is plotted at the center of each bin.
   //
   //  chopt='P' :  Idem with the current marker
   //  chopt='P0':  Idem with the current marker. Empty bins also drawn
   //
   //  chopt='B' :  A Bar chart with equidistant bins is drawn as fill
   //               areas (Contours are drawn).
   //
   //  chopt='9' :  Force graph to be drawn in high resolution mode.
   //               By default, the graph is drawn in low resolution
   //               in case the number of points is greater than the number of pixels
   //               in the current pad.
   //
   //  chopt='][' : "Cutoff" style. When this option is selected together with
   //               H option, the first and last vertical lines of the histogram
   //               are not drawn.

   const char *where = "PaintGraphHist";

   Int_t optionLine , optionAxis , optionCurve, optionStar , optionMark;
   Int_t optionBar  , optionRot  , optionOne  , optionOff;
   Int_t optionFill , optionZ;
   Int_t optionHist , optionBins , optionMarker;
   Int_t i, j, npt;
   Int_t drawtype=0, drawborder, drawbordersav;
   Double_t xlow, xhigh, ylow, yhigh;
   Double_t wmin, wmax;
   Double_t dbar, offset, wminstep;
   Double_t delta = 0;
   Double_t ylast = 0;
   Double_t xi, xi1, xj, xj1, yi1, yi, yj, yj1, xwmin, ywmin;
   Int_t first, last, nbins;
   Int_t fillarea;

   char choptaxis[10] = " ";

   if (npoints <= 0) {
      Error(where, "illegal number of points (%d)", npoints);
      return;
   }
   TString opt = chopt;
   opt.ToUpper();
   if(opt.Contains("H"))  optionHist = 1;  else optionHist = 0;
   if(opt.Contains("F"))  optionFill = 1;  else optionFill = 0;
   if(opt.Contains("C"))  optionCurve= 1;  else optionCurve= 0;
   if(opt.Contains("*"))  optionStar = 1;  else optionStar = 0;
   if(opt.Contains("R"))  optionRot  = 1;  else optionRot  = 0;
   if(opt.Contains("1"))  optionOne  = 1;  else optionOne  = 0;
   if(opt.Contains("B"))  optionBar  = 1;  else optionBar  = 0;
   if(opt.Contains("N"))  optionBins = 1;  else optionBins = 0;
   if(opt.Contains("L"))  optionLine = 1;  else optionLine = 0;
   if(opt.Contains("P"))  optionMark = 1;  else optionMark = 0;
   if(opt.Contains("A"))  optionAxis = 1;  else optionAxis = 0;
   if(opt.Contains("][")) optionOff  = 1;  else optionOff  = 0;
   if(opt.Contains("P0")) optionMark = 10;

   Int_t optionFill2 = 0;
   if(opt.Contains("F") && opt.Contains("2")) {
      optionFill = 0; optionFill2 = 1;
   }

   // Set Clipping option
   Option_t *noClip;
   if (TestBit(TGraph::kClipFrame)) noClip = "";
   else                     noClip = "C";
   gPad->SetBit(TGraph::kClipFrame, TestBit(TGraph::kClipFrame));

   optionZ = 1;

   if (optionStar) theGraph->SetMarkerStyle(3);

   first = 1;
   last  = npoints;
   nbins = last - first + 1;

   //           Draw the Axis with a fixed number of division: 510

   Double_t baroffset = gStyle->GetBarOffset();
   Double_t barwidth  = gStyle->GetBarWidth();
   Double_t rwxmin    = gPad->GetUxmin();
   Double_t rwxmax    = gPad->GetUxmax();
   Double_t rwymin    = gPad->GetUymin();
   Double_t rwymax    = gPad->GetUymax();
   Double_t uxmin     = gPad->PadtoX(rwxmin);
   Double_t uxmax     = gPad->PadtoX(rwxmax);
   Double_t rounding  = (uxmax-uxmin)*1.e-5;
   drawborder         = gStyle->GetDrawBorder();
   if (optionAxis) {
      Int_t nx1, nx2, ndivx, ndivy, ndiv;
      choptaxis[0]  = 0;
      Double_t rwmin  = rwxmin;
      Double_t rwmax  = rwxmax;
      ndivx = gStyle->GetNdivisions("X");
      ndivy = gStyle->GetNdivisions("Y");
      if (ndivx > 1000) {
         nx2   = ndivx/100;
         nx1   = TMath::Max(1, ndivx%100);
         ndivx = 100*nx2 + Int_t(Double_t(nx1)*gPad->GetAbsWNDC());
      }
      ndiv  =TMath::Abs(ndivx);
      if (ndivx < 0) strcat(choptaxis, "N");
      if (gPad->GetGridx()) {
         strcat(choptaxis, "W");
      }
      if (gPad->GetLogx()) {
         rwmin = TMath::Power(10,rwxmin);
         rwmax = TMath::Power(10,rwxmax);
         strcat(choptaxis, "G");
      }
      TGaxis *axis = new TGaxis();
      axis->SetLineColor(gStyle->GetAxisColor("X"));
      axis->SetTextColor(gStyle->GetLabelColor("X"));
      axis->SetTextFont(gStyle->GetLabelFont("X"));
      axis->SetLabelSize(gStyle->GetLabelSize("X"));
      axis->SetLabelOffset(gStyle->GetLabelOffset("X"));
      axis->SetTickSize(gStyle->GetTickLength("X"));

      axis->PaintAxis(rwxmin,rwymin,rwxmax,rwymin,rwmin,rwmax,ndiv,choptaxis);

      choptaxis[0]  = 0;
      rwmin  = rwymin;
      rwmax  = rwymax;
      if (ndivy < 0) {
         nx2   = ndivy/100;
         nx1   = TMath::Max(1, ndivy%100);
         ndivy = 100*nx2 + Int_t(Double_t(nx1)*gPad->GetAbsHNDC());
         strcat(choptaxis, "N");
      }
      ndiv  =TMath::Abs(ndivy);
      if (gPad->GetGridy()) {
         strcat(choptaxis, "W");
      }
      if (gPad->GetLogy()) {
         rwmin = TMath::Power(10,rwymin);
         rwmax = TMath::Power(10,rwymax);
         strcat(choptaxis,"G");
      }
      axis->SetLineColor(gStyle->GetAxisColor("Y"));
      axis->SetTextColor(gStyle->GetLabelColor("Y"));
      axis->SetTextFont(gStyle->GetLabelFont("Y"));
      axis->SetLabelSize(gStyle->GetLabelSize("Y"));
      axis->SetLabelOffset(gStyle->GetLabelOffset("Y"));
      axis->SetTickSize(gStyle->GetTickLength("Y"));

      axis->PaintAxis(rwxmin,rwymin,rwxmin,rwymax,rwmin,rwmax,ndiv,choptaxis);
      delete axis;
   }


   //           Set attributes
   theGraph->TAttLine::Modify();
   theGraph->TAttFill::Modify();
   theGraph->TAttMarker::Modify();

   //       Min-Max scope

   if (!optionRot) {wmin = x[0];   wmax = x[1];}
   else            {wmin = y[0];   wmax = y[1];}

   if (!optionBins) delta = (wmax - wmin)/ Double_t(nbins);

   Int_t fwidth = gPad->GetFrameLineWidth();
   TFrame *frame = gPad->GetFrame();
   if (frame) fwidth = frame->GetLineWidth();
   Double_t dxframe = gPad->AbsPixeltoX(fwidth/2) - gPad->AbsPixeltoX(0);
   Double_t vxmin = gPad->PadtoX(gPad->GetUxmin() + dxframe);
   Double_t vxmax = gPad->PadtoX(gPad->GetUxmax() - dxframe);
   Double_t dyframe = -gPad->AbsPixeltoY(fwidth/2) + gPad->AbsPixeltoY(0);
   Double_t vymin = gPad->GetUymin() + dyframe; //y already in log scale
   vxmin = TMath::Max(vxmin,wmin);
   vxmax = TMath::Min(vxmax,wmax);

   //           Draw the histogram with a fill area

   gxwork  = new Double_t[2*npoints+10];
   gywork  = new Double_t[2*npoints+10];
   gxworkl = new Double_t[2*npoints+10];
   gyworkl = new Double_t[2*npoints+10];

   if (optionFill && !optionCurve) {
      fillarea = kTRUE;
      if (!optionRot) {
         gxwork[0] = vxmin;
         if (!optionOne) gywork[0] = TMath::Min(TMath::Max((Double_t)0,gPad->GetUymin())
                                               ,gPad->GetUymax());
         else            gywork[0] = gPad->GetUymin();
         npt = 2;
         for (j=first; j<=last;j++) {
            if (!optionBins) {
               gxwork[npt-1]   = gxwork[npt-2];
               gxwork[npt]     = wmin+((j-first+1)*delta);
               if (gxwork[npt] < gxwork[0]) gxwork[npt] = gxwork[0];

            }
            else {
               xj1 = x[j];      xj  = x[j-1];
               if (xj1 < xj) {
                  if (j != last) Error(where, "X must be in increasing order");
                  else           Error(where, "X must have N+1 values with option N");
                  return;
               }
               gxwork[npt-1] = x[j-1];       gxwork[npt] = x[j];
            }
            gywork[npt-1] = y[j-1];
            gywork[npt]   = y[j-1];
            if (gywork[npt] < vymin) {gywork[npt] = vymin; gywork[npt-1] = vymin;}
            if (gxwork[npt-1] >= uxmin-rounding && gxwork[npt] <= uxmax+rounding) npt += 2;
            else gxwork[npt-2] = TMath::Min(gxwork[npt], uxmax);
            if (j == last) {
               gxwork[npt-1] = gxwork[npt-2];
               gywork[npt-1] = gywork[0];
               //make sure that the fill area does not overwrite the frame
               //take into account the frame linewidth
               if (gxwork[0    ] < vxmin) {gxwork[0    ] = vxmin; gxwork[1    ] = vxmin;}
               if (gywork[0] < vymin) {gywork[0] = vymin; gywork[npt-1] = vymin;}

               //transform to log ?
               ComputeLogs(npt, optionZ);
               gPad->PaintFillArea(npt,gxworkl,gyworkl);
               if (drawborder) {
                  if (!fillarea) gyworkl[0] = ylast;
                  gPad->PaintPolyLine(npt-1,gxworkl,gyworkl,noClip);
               }
               continue;
            }
         }  //endfor (j=first; j<=last;j++) {
      }
      else {
         gywork[0] = wmin;
         if (!optionOne) gxwork[0] = TMath::Max((Double_t)0,gPad->GetUxmin());
         else            gxwork[0] = gPad->GetUxmin();
         npt = 2;
         for (j=first; j<=last;j++) {
            if (!optionBins) {
               gywork[npt-1] = gywork[npt-2];
               gywork[npt]   = wmin+((j-first+1)*delta);
            }
            else {
               yj1 = y[j];      yj  = y[j-1];
               if (yj1 < yj) {
                  if (j != last) Error(where, "Y must be in increasing order");
                  else           Error(where, "Y must have N+1 values with option N");
                  return;
               }
               gywork[npt-1] = y[j-1];       gywork[npt] = y[j];
            }
            gxwork[npt-1] = x[j-1];      gxwork[npt] = x[j-1];
            if (gxwork[npt-1] >= uxmin-rounding && gxwork[npt] <= uxmax+rounding) npt += 2;
            if (j == last) {
               gywork[npt-1] = gywork[npt-2];
               gxwork[npt-1] = gxwork[0];
               ComputeLogs(npt, optionZ);
               gPad->PaintFillArea(npt,gxworkl,gyworkl);
               if (drawborder) {
                  if (!fillarea) gyworkl[0] = ylast;
                  gPad->PaintPolyLine(npt-1,gxworkl,gyworkl,noClip);
               }
               continue;
            }
         }  //endfor (j=first; j<=last;j++)
      }
      theGraph->TAttLine::Modify();
      theGraph->TAttFill::Modify();
   }

   //      Draw a standard Histogram (default)

   if ((optionHist) || strlen(chopt) == 0) {
      if (!optionRot) {
         gxwork[0] = wmin;
         gywork[0] = gPad->GetUymin();
         ywmin    = gywork[0];
         npt      = 2;
         for (i=first; i<=last;i++) {
            if (!optionBins) {
               gxwork[npt-1] = gxwork[npt-2];
               gxwork[npt]   = wmin+((i-first+1)*delta);
            }
            else {
               xi1 = x[i];      xi  = x[i-1];
               if (xi1 < xi) {
                  if (i != last) Error(where, "X must be in increasing order");
                  else           Error(where, "X must have N+1 values with option N");
                  return;
               }
               gxwork[npt-1] = x[i-1];      gxwork[npt] = x[i];
            }
            gywork[npt-1] = y[i-1];
            gywork[npt]   = y[i-1];
            if (gywork[npt] < vymin) {gywork[npt] = vymin; gywork[npt-1] = vymin;}
            if (gxwork[npt-1] >= uxmin-rounding && gxwork[npt] <= uxmax+rounding) npt += 2;
            else gxwork[npt-2] = TMath::Min(gxwork[npt], uxmax);
            if (i == last) {
               gxwork[npt-1] = gxwork[npt-2];
               gywork[npt-1] = gywork[0];
               //make sure that the fill area does not overwrite the frame
               //take into account the frame linewidth
               if (gxwork[0    ] < vxmin) {gxwork[0    ] = vxmin; gxwork[1    ] = vxmin;}
               if (gywork[0] < vymin) {gywork[0] = vymin; gywork[npt-1] = vymin;}

               ComputeLogs(npt, optionZ);

               //do not draw the two vertical lines on the edges
               Int_t nbpoints = npt-2;
               Int_t point1  = 1;
               if (optionOff) {
                  // remove points before the low cutoff
                  Int_t ip;
                  for (ip=point1; ip<=nbpoints; ip++) {
                     if (gyworkl[ip] != ywmin) {
                        point1 = ip;
                        break;
                     }
                  }
                  // remove points after the high cutoff
                  Int_t point2 = nbpoints;
                  for (ip=point2; ip>=point1; ip--) {
                     if (gyworkl[ip] != ywmin) {
                        point2 = ip;
                        break;
                     }
                  }
                  nbpoints = point2-point1+1;
               }
               gPad->PaintPolyLine(nbpoints,&gxworkl[point1],&gyworkl[point1],noClip);
               continue;
            }
         }  //endfor (i=first; i<=last;i++)
      }
      else {
         gywork[0] = wmin;
         gxwork[0] = TMath::Max((Double_t)0,gPad->GetUxmin());
         xwmin    = gxwork[0];
         npt      = 2;
         for (i=first; i<=last;i++) {
            if (!optionBins) {
               gywork[npt-1]   = gywork[npt-2];
               gywork[npt] = wmin+((i-first+1)*delta);
            }
            else {
               yi1 = y[i];      yi  = y[i-1];
               if (yi1 < yi) {
                  if (i != last) Error(where, "Y must be in increasing order");
                  else           Error(where, "Y must have N+1 values with option N");
                  return;
               }
               gywork[npt-1] = y[i-1];      gywork[npt] = y[i];
            }
            gxwork[npt-1] = x[i-1];      gxwork[npt] = x[i-1];
            if (gxwork[npt-1] >= uxmin-rounding && gxwork[npt] <= uxmax+rounding) npt += 2;
            if (i == last) {
               gywork[npt-1] = gywork[npt-2];
               gxwork[npt-1] = xwmin;
               ComputeLogs(npt, optionZ);
               gPad->PaintPolyLine(npt,gxworkl,gyworkl,noClip);
               continue;
            }
         }  //endfor (i=first; i<=last;i++)
      }
   }

   //              Draw the histogram with a smooth Curve. The computing
   //              of the smoothing is done by the routine IGRAp1

   if (optionCurve) {
      if (!optionFill) drawtype = 1;
      else {
         if (!optionOne) drawtype = 2;
         else            drawtype = 3;
      }
      if (!optionRot) {
         npt = 0;
         for (i=first; i<=last;i++) {
            npt++;
            if (!optionBins) gxwork[npt-1] = wmin+(i-first)*delta+0.5*delta;
            else {
               xi1 = x[i];      xi  = x[i-1];
               if (xi1 < xi) {
                  if (i != last) Error(where, "X must be in increasing order");
                  else           Error(where, "X must have N+1 values with option N");
                  return;
               }
               gxwork[npt-1] = x[i-1] + 0.5*(x[i]-x[i-1]);
            }
            if (gxwork[npt-1] < uxmin || gxwork[npt-1] > uxmax) {
               npt--;
               continue;
            }
            gywork[npt-1] = y[i-1];
            ComputeLogs(npt, optionZ);
            if ((gyworkl[npt-1] < rwymin) || (gyworkl[npt-1] > rwymax)) {
               if (npt > 2) {
                  ComputeLogs(npt, optionZ);
                  Smooth(theGraph, npt,gxworkl,gyworkl,drawtype);
               }
               gxwork[0] = gxwork[npt-1];
               gywork[0] = gywork[npt-1];
               npt      = 1;
               continue;
            }
            if (npt >= 50) {
               ComputeLogs(50, optionZ);
               Smooth(theGraph, 50,gxworkl,gyworkl,drawtype);
               gxwork[0] = gxwork[npt-1];
               gywork[0] = gywork[npt-1];
               npt      = 1;
            }
         }  //endfor (i=first; i<=last;i++)
         if (npt > 1) {
            ComputeLogs(npt, optionZ);
            Smooth(theGraph, npt,gxworkl,gyworkl,drawtype);
         }
      }
      else {
         drawtype = drawtype+10;
         npt   = 0;
         for (i=first; i<=last;i++) {
            npt++;
            if (!optionBins) gywork[npt-1] = wmin+(i-first)*delta+0.5*delta;
            else {
               yi1 = y[i];      yi = y[i-1];
               if (yi1 < yi) {
                  if (i != last) Error(where, "Y must be in increasing order");
                  else           Error(where, "Y must have N+1 values with option N");
                  return;
               }
               gywork[npt-1] = y[i-1] + 0.5*(y[i]-y[i-1]);
            }
            gxwork[npt-1] = x[i-1];
            ComputeLogs(npt, optionZ);
            if ((gxworkl[npt] < uxmin) || (gxworkl[npt] > uxmax)) {
               if (npt > 2) {
                  ComputeLogs(npt, optionZ);
                  Smooth(theGraph, npt,gxworkl,gyworkl,drawtype);
               }
               gxwork[0] = gxwork[npt-1];
               gywork[0] = gywork[npt-1];
               npt      = 1;
               continue;
            }
            if (npt >= 50) {
               ComputeLogs(50, optionZ);
               Smooth(theGraph, 50,gxworkl,gyworkl,drawtype);
               gxwork[0] = gxwork[npt-1];
               gywork[0] = gywork[npt-1];
               npt      = 1;
            }
         }  //endfor (i=first; i<=last;i++)
         if (npt > 1) {
            ComputeLogs(npt, optionZ);
            Smooth(theGraph, npt,gxworkl,gyworkl,drawtype);
         }
      }
   }

   //    Draw the histogram with a simple line or/and a marker

   optionMarker = 0;
   if ((optionStar) || (optionMark))optionMarker=1;
   if ((optionMarker) || (optionLine)) {
      wminstep = wmin + 0.5*delta;
      Axis_t ax1,ax2,ay1,ay2;
      gPad->GetRangeAxis(ax1,ay1,ax2,ay2);

      Int_t ax1Pix = gPad->XtoAbsPixel(ax1);
      Int_t ax2Pix = gPad->XtoAbsPixel(ax2);
      Int_t ay1Pix = gPad->YtoAbsPixel(ay1);
      Int_t ay2Pix = gPad->YtoAbsPixel(ay2);

      Int_t nrPix;
      if (!optionRot)
         nrPix = ax2Pix-ax1Pix+1;
      else
         nrPix = ay2Pix-ay1Pix+1;

      // Make here decision whether it should be painted in high or low resolution
      Int_t ip, ipix, lowRes = 0;
      if (3*nrPix < last-first+1) {
         lowRes = 1;
      }
      if (optionFill2)       lowRes = 0;
      if (opt.Contains("9")) lowRes = 0;
      if (lowRes) {
         Double_t *minPix   = new Double_t[nrPix];
         Double_t *maxPix   = new Double_t[nrPix];
         Double_t *centrPix = new Double_t[nrPix];
         Int_t *nrEntries   = new Int_t[nrPix];

         for (ip = 0; ip < nrPix; ip++) {
            minPix[ip]    =  1e100;
            maxPix[ip]    = -1e100;
            nrEntries[ip] = 0;
         }

         for (ip = first; ip < last; ip++) {
            Double_t xw;
            if (!optionBins) xw = wmin + (ip-first)*delta+0.5*delta;
            else             xw = x[ip-1] + 0.5*(x[ip]-x[ip-1]);;

            if (!optionRot) {
               Int_t ix = gPad->XtoAbsPixel(gPad->XtoPad(xw))-ax1Pix;
               if (ix < 0) ix = 0;
               if (ix >= nrPix) ix = nrPix-1;
               Int_t yPixel = gPad->YtoAbsPixel(y[ip-1]);
               if (yPixel >= ay1Pix) continue;
               if (minPix[ix] > yPixel) minPix[ix] = yPixel;
               if (maxPix[ix] < yPixel) maxPix[ix] = yPixel;
               (nrEntries[ix])++;
            } else {
               Int_t iy = gPad->YtoAbsPixel(gPad->YtoPad(y[ip-1]))-ay1Pix;
               if (iy < 0) iy = 0;
               if (iy >= nrPix) iy = nrPix-1;;
               Int_t xPixel = gPad->XtoAbsPixel(gPad->XtoPad(xw));
               if (minPix[iy] > xPixel) minPix[iy] = xPixel;
               if (maxPix[iy] < xPixel) maxPix[iy] = xPixel;
               (nrEntries[iy])++;
            }
         }

         for (ipix = 0; ipix < nrPix; ipix++) {
            if (nrEntries[ipix] > 0)
               centrPix[ipix] = (minPix[ipix]+maxPix[ipix])/2.0;
            else
               centrPix[ipix] = 2*TMath::Max(TMath::Abs(minPix[ipix]),
                                             TMath::Abs(maxPix[ipix]));
         }

         Double_t *xc = new Double_t[nrPix];
         Double_t *yc = new Double_t[nrPix];

         Double_t xcadjust = 0.3*(gPad->AbsPixeltoX(ax1Pix+1) - gPad->AbsPixeltoX(ax1Pix));
         Double_t ycadjust = 0.3*(gPad->AbsPixeltoY(ay1Pix)   - gPad->AbsPixeltoY(ay1Pix+1));
         Int_t nrLine = 0;
         for (ipix = 0; ipix < nrPix; ipix++) {
            if (minPix[ipix] <= maxPix[ipix]) {
               Double_t xl[2]; Double_t yl[2];
               if (!optionRot) {
                  xc[nrLine] = gPad->AbsPixeltoX(ax1Pix+ipix) + xcadjust;
                  yc[nrLine] = gPad->AbsPixeltoY((Int_t)centrPix[ipix]);

                  xl[0]      = xc[nrLine];
                  yl[0]      = gPad->AbsPixeltoY((Int_t)minPix[ipix]);
                  xl[1]      = xc[nrLine];
                  yl[1]      = gPad->AbsPixeltoY((Int_t)maxPix[ipix]);
               } else {
                  yc[nrLine] = gPad->AbsPixeltoY(ay1Pix+ipix) + ycadjust;
                  xc[nrLine] = gPad->AbsPixeltoX((Int_t)centrPix[ipix]);

                  xl[0]      = gPad->AbsPixeltoX((Int_t)minPix[ipix]);
                  yl[0]      = yc[nrLine];
                  xl[1]      = gPad->AbsPixeltoX((Int_t)maxPix[ipix]);
                  yl[1]      = yc[nrLine];
               }
               if (!optionZ && gPad->GetLogx()) {
                  if (xc[nrLine] > 0) xc[nrLine] = TMath::Log10(xc[nrLine]);
                  else                xc[nrLine] = gPad->GetX1();
                  for (Int_t il = 0; il < 2; il++) {
                     if (xl[il] > 0) xl[il] = TMath::Log10(xl[il]);
                     else            xl[il] = gPad->GetX1();
                  }
               }
               if (!optionZ && gPad->GetLogy()) {
                  if (yc[nrLine] > 0) yc[nrLine] = TMath::Log10(yc[nrLine]);
                  else                yc[nrLine] = gPad->GetY1();
                  for (Int_t il = 0; il < 2; il++) {
                     if (yl[il] > 0) yl[il] = TMath::Log10(yl[il]);
                     else            yl[il] = gPad->GetY1();
                  }
               }

               gPad->PaintPolyLine(2,xl,yl,noClip);
               nrLine++;
            }
         }

         gPad->PaintPolyLine(nrLine,xc,yc,noClip);

         delete [] xc;
         delete [] yc;

         delete [] minPix;
         delete [] maxPix;
         delete [] centrPix;
         delete [] nrEntries;
      } else {
         if (!optionRot) {
            npt = 0;
            for (i=first; i<=last;i++) {
               npt++;
               if (!optionBins) gxwork[npt-1] = wmin+(i-first)*delta+0.5*delta;
               else {
                  xi1 = x[i];      xi = x[i-1];
                  if (xi1 < xi) {
                     if (i != last) Error(where, "X must be in increasing order");
                     else           Error(where, "X must have N+1 values with option N");
                     return;
                  }
                  gxwork[npt-1] = x[i-1] + 0.5*(x[i]-x[i-1]);
               }
               if (gxwork[npt-1] < uxmin || gxwork[npt-1] > uxmax) { npt--; continue;}
               if ((optionMark != 10) && (optionLine == 0)) {
                  if (y[i-1] <= rwymin)  {npt--; continue;}
               }
               gywork[npt-1] = y[i-1];
               gywork[npt]   = y[i-1]; //new
               if ((gywork[npt-1] < rwymin) || ((gywork[npt-1] > rwymax) && !optionFill2)) {
                  if ((gywork[npt-1] < rwymin)) gywork[npt-1] = rwymin;
                  if ((gywork[npt-1] > rwymax)) gywork[npt-1] = rwymax;
                  if (npt > 2) {
                     if (optionMarker) {
                        ComputeLogs(npt, optionZ);
                        gPad->PaintPolyMarker(npt,gxworkl,gyworkl);
                     }
                     if (optionLine) {
                        if (!optionMarker) ComputeLogs(npt, optionZ);
                        gPad->PaintPolyLine(npt,gxworkl,gyworkl,noClip);
                     }
                  }
                  gxwork[0] = gxwork[npt-1];
                  gywork[0] = gywork[npt-1];
                  npt       = 1;
                  continue;
               }

               if (npt >= 50) {
                  if (optionMarker) {
                     ComputeLogs(50, optionZ);
                     gPad->PaintPolyMarker(50,gxworkl,gyworkl);
                  }
                  if (optionLine) {
                     if (!optionMarker) ComputeLogs(50, optionZ);
                     if (optionFill2) {
                        gxworkl[npt]   = gxworkl[npt-1]; gyworkl[npt]   = rwymin;
                        gxworkl[npt+1] = gxworkl[0];     gyworkl[npt+1] = rwymin;
                        gPad->PaintFillArea(52,gxworkl,gyworkl);
                     }
                     gPad->PaintPolyLine(50,gxworkl,gyworkl);
                  }
                  gxwork[0] = gxwork[npt-1];
                  gywork[0] = gywork[npt-1];
                  npt      = 1;
               }
            }  //endfor (i=first; i<=last;i++)
            if (optionMarker && npt > 0) {
               ComputeLogs(npt, optionZ);
               gPad->PaintPolyMarker(npt,gxworkl,gyworkl);
            }
            if (optionLine && npt > 1) {
               if (!optionMarker) ComputeLogs(npt, optionZ);
               if (optionFill2) {
                  gxworkl[npt]   = gxworkl[npt-1]; gyworkl[npt]   = rwymin;
                  gxworkl[npt+1] = gxworkl[0];     gyworkl[npt+1] = rwymin;
                  gPad->PaintFillArea(npt+2,gxworkl,gyworkl);
               }
               gPad->PaintPolyLine(npt,gxworkl,gyworkl);
            }
         }
         else {
            npt = 0;
            for (i=first; i<=last;i++) {
               npt++;
               if (!optionBins) gywork[npt-1] = wminstep+(i-first)*delta+0.5*delta;
               else {
                  yi1 = y[i];      yi = y[i-1];
                  if (yi1 < yi) {
                     if (i != last) Error(where, "Y must be in increasing order");
                     else           Error(where, "Y must have N+1 values with option N");
                     return;
                  }
                  gywork[npt-1] = y[i-1] + 0.5*(y[i]-y[i-1]);
               }
               gxwork[npt-1] = x[i-1];
               if ((gxwork[npt-1] < uxmin) || (gxwork[npt-1] > uxmax)) {
                  if (npt > 2) {
                     if (optionMarker) {
                        ComputeLogs(npt, optionZ);
                        gPad->PaintPolyMarker(npt,gxworkl,gyworkl);
                     }
                     if (optionLine) {
                        if (!optionMarker) ComputeLogs(npt, optionZ);
                        gPad->PaintPolyLine(npt,gxworkl,gyworkl,noClip);
                     }
                  }
                  gxwork[0] = gxwork[npt-1];
                  gywork[0] = gywork[npt-1];
                  npt      = 1;
                  continue;
               }
               if (npt >= 50) {
                  if (optionMarker) {
                     ComputeLogs(50, optionZ);
                     gPad->PaintPolyMarker(50,gxworkl,gyworkl);
                  }
                  if (optionLine) {
                     if (!optionMarker) ComputeLogs(50, optionZ);
                     gPad->PaintPolyLine(50,gxworkl,gyworkl);
                  }
                  gxwork[0] = gxwork[npt-1];
                  gywork[0] = gywork[npt-1];
                  npt      = 1;
               }
            }  //endfor (i=first; i<=last;i++)
            if (optionMarker && npt > 0) {
               ComputeLogs(npt, optionZ);
               gPad->PaintPolyMarker(npt,gxworkl,gyworkl);
            }
            if (optionLine != 0 && npt > 1) {
               if (!optionMarker) ComputeLogs(npt, optionZ);
               gPad->PaintPolyLine(npt,gxworkl,gyworkl,noClip);
            }
         }
      }
   }

   //              Draw the histogram as a bar chart

   if (optionBar) {
      if (!optionBins) { offset = delta*baroffset; dbar = delta*barwidth; }
      else {
         if (!optionRot) {
            offset = (x[1]-x[0])*baroffset;
            dbar   = (x[1]-x[0])*barwidth;
         } else {
            offset = (y[1]-y[0])*baroffset;
            dbar   = (y[1]-y[0])*barwidth;
         }
      }
      drawbordersav = drawborder;
      gStyle->SetDrawBorder(1);
      if (!optionRot) {
         xlow  = wmin+offset;
         xhigh = wmin+offset+dbar;
         if (!optionOne) ylow = TMath::Max((Double_t)0,gPad->GetUymin());
         else            ylow = gPad->GetUymin();

         for (i=first; i<=last;i++) {
            yhigh    = y[i-1];
            gxwork[0] = xlow;
            gywork[0] = ylow;
            gxwork[1] = xhigh;
            gywork[1] = yhigh;
            ComputeLogs(2, optionZ);
            gPad->PaintBox(gxworkl[0],gyworkl[0],gxworkl[1],gyworkl[1]);
            if (!optionBins) {
               xlow  = xlow+delta;
               xhigh = xhigh+delta;
            }
            else {
               if (i < last) {
                  xi1 = x[i];      xi = x[i-1];
                  if (xi1 < xi) {
                     Error(where, "X must be in increasing order");
                     return;
                  }
                  offset  = (x[i+1]-x[i])*baroffset;
                  dbar    = (x[i+1]-x[i])*barwidth;
                  xlow    = x[i] + offset;
                  xhigh   = x[i] + offset + dbar;
               }
            }
         }  //endfor (i=first; i<=last;i++)
      }
      else {
         ylow  = wmin + offset;
         yhigh = wmin + offset + dbar;
         if (!optionOne) xlow = TMath::Max((Double_t)0,gPad->GetUxmin());
         else            xlow = gPad->GetUxmin();

         for (i=first; i<=last;i++) {
            xhigh    = x[i-1];
            gxwork[0] = xlow;
            gywork[0] = ylow;
            gxwork[1] = xhigh;
            gywork[1] = yhigh;
            ComputeLogs(2, optionZ);
            gPad->PaintBox(gxworkl[0],gyworkl[0],gxworkl[1],gyworkl[1]);
            gPad->PaintBox(xlow,ylow,xhigh,yhigh);
            if (!optionBins) {
               ylow  = ylow  + delta;
               yhigh = yhigh + delta;
            }
            else {
               if (i < last) {
                  yi1 = y[i];      yi = y[i-1];
                  if (yi1 < yi) {
                     Error(where, "Y must be in increasing order");
                     return;
                  }
                  offset  = (y[i+1]-y[i])*baroffset;
                  dbar    = (y[i+1]-y[i])*barwidth;
                  ylow    = y[i] + offset;
                  yhigh   = y[i] + offset + dbar;
               }
            }
         }  //endfor (i=first; i<=last;i++)
      }
      gStyle->SetDrawBorder(drawbordersav);
   }
   gPad->ResetBit(TGraph::kClipFrame);

   delete [] gxwork;
   delete [] gywork;
   delete [] gxworkl;
   delete [] gyworkl;
}


//______________________________________________________________________________
void TGraphPainter::PaintGraphAsymmErrors(TGraph *theGraph, Option_t *option)
{
   // Paint this TGraphAsymmErrors with its current attributes
   //
   // by default horizonthal and vertical small lines are drawn at
   // the end of the error bars. if option "z" or "Z" is specified,
   // these lines are not drawn.
   //
   // if option contains ">" an arrow is drawn at the end of the error bars
   // if option contains "|>" a full arrow is drawn at the end of the error bars
   // the size of the arrow is set to 2/3 of the marker size.
   //
   // By default, error bars are drawn. If option "X" is specified,
   // the errors are not drawn (TGraph::Paint equivalent).
   //
   // if option "[]" is specified only the end vertical/horizonthal lines
   // of the error bars are drawn. This option is interesting to superimpose
   // systematic errors on top of a graph with statistical errors.
   //
   // if option "2" is specified error rectangles are drawn.
   //
   // if option "3" is specified a filled area is drawn through the end points >
   // the vertical error bars.
   //
   // if option "4" is specified a smoothed filled area is drawn through the end
   // points of the vertical error bars.

   Double_t *xline = 0;
   Double_t *yline = 0;
   Int_t if1 = 0;
   Int_t if2 = 0;

   const Int_t kBASEMARKER=8;
   Double_t s2x, s2y, symbolsize, sbase;
   Double_t x, y, xl1, xl2, xr1, xr2, yup1, yup2, ylow1, ylow2, tx, ty;
   static Float_t cxx[11] = {1,1,0.6,0.6,1,1,0.6,0.5,1,0.6,0.6};
   static Float_t cyy[11] = {1,1,1,1,1,1,1,1,1,0.5,0.6};
   Int_t theNpoints = theGraph->GetN();
   Double_t *theX  = theGraph->GetX();
   Double_t *theY  = theGraph->GetY();
   Double_t *theEXlow  = theGraph->GetEXlow();
   Double_t *theEYlow  = theGraph->GetEYlow();
   Double_t *theEXhigh = theGraph->GetEXhigh();
   Double_t *theEYhigh = theGraph->GetEYhigh();

   if (strchr(option,'X') || strchr(option,'x')) {PaintGraphSimple(theGraph, option); return;}
   Bool_t brackets = kFALSE;
   if (strstr(option,"[]")) brackets = kTRUE;
   Bool_t endLines = kTRUE;
   if (strchr(option,'z')) endLines = kFALSE;
   if (strchr(option,'Z')) endLines = kFALSE;
   const char *arrowOpt = 0;
   if (strchr(option,'>'))  arrowOpt = ">";
   if (strstr(option,"|>")) arrowOpt = "|>";

   Bool_t axis = kFALSE;
   if (strchr(option,'a')) axis = kTRUE;
   if (strchr(option,'A')) axis = kTRUE;
   if (axis) PaintGraphSimple(theGraph, option);

   Bool_t option2 = kFALSE;
   Bool_t option3 = kFALSE;
   Bool_t option4 = kFALSE;
   if (strchr(option,'2')) option2 = kTRUE;
   if (strchr(option,'3')) option3 = kTRUE;
   if (strchr(option,'4')) {option3 = kTRUE; option4 = kTRUE;}

   if (option3) {
      xline = new Double_t[2*theNpoints];
      yline = new Double_t[2*theNpoints];
      if (!xline || !yline) {
         Error("Paint", "too many points, out of memory");
         return;
      }
      if1 = 1;
      if2 = 2*theNpoints;
   }

   theGraph->TAttLine::Modify();

   TArrow arrow;
   arrow.SetLineWidth(theGraph->GetLineWidth());
   arrow.SetLineColor(theGraph->GetLineColor());
   arrow.SetFillColor(theGraph->GetFillColor());

   TBox box;
   box.SetLineWidth(theGraph->GetLineWidth());
   box.SetLineColor(theGraph->GetLineColor());
   box.SetFillColor(theGraph->GetFillColor());
   box.SetFillStyle(theGraph->GetFillStyle());

   symbolsize  = theGraph->GetMarkerSize();
   sbase       = symbolsize*kBASEMARKER;
   Int_t mark  = theGraph->GetMarkerStyle();
   Double_t cx  = 0;
   Double_t cy  = 0;
   if (mark >= 20 && mark < 31) {
      cx = cxx[mark-20];
      cy = cyy[mark-20];
   }

   // Define the offset of the error bars due to the symbol size
   s2x  = gPad->PixeltoX(Int_t(0.5*sbase)) - gPad->PixeltoX(0);
   s2y  =-gPad->PixeltoY(Int_t(0.5*sbase)) + gPad->PixeltoY(0);
   Int_t dxend = Int_t(gStyle->GetEndErrorSize());
   tx    = gPad->PixeltoX(dxend) - gPad->PixeltoX(0);
   ty    =-gPad->PixeltoY(dxend) + gPad->PixeltoY(0);
   Float_t asize = 0.6*symbolsize*kBASEMARKER/gPad->GetWh();

   gPad->SetBit(TGraph::kClipFrame, TestBit(TGraph::kClipFrame));
   for (Int_t i=0;i<theNpoints;i++) {
      x  = gPad->XtoPad(theX[i]);
      y  = gPad->YtoPad(theY[i]);
      if (x < gPad->GetUxmin()) continue;
      if (x > gPad->GetUxmax()) continue;
      if (y < gPad->GetUymin()) continue;
      if (y > gPad->GetUymax()) continue;
      xl1 = x - s2x*cx;
      xl2 = gPad->XtoPad(theX[i] - theEXlow[i]);

      //  draw the error rectangles
      if (option2) {
         box.PaintBox(gPad->XtoPad(theX[i] - theEXlow[i]),
                      gPad->YtoPad(theY[i] - theEYlow[i]),
                      gPad->XtoPad(theX[i] + theEXhigh[i]),
                      gPad->YtoPad(theY[i] + theEYhigh[i]));
         continue;
      }

      //  keep points for fill area drawing
      if (option3) {
         xline[if1-1] = x;
         xline[if2-1] = x;
         yline[if1-1] = gPad->YtoPad(theY[i] + theEYhigh[i]);
         yline[if2-1] = gPad->YtoPad(theY[i] - theEYlow[i]);
         if1++;
         if2--;
         continue;
      }

      if (xl1 > xl2) {
         if (arrowOpt) {
            arrow.PaintArrow(xl1,y,xl2,y,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(xl1,y,xl2,y);
            if (endLines)  gPad->PaintLine(xl2,y-ty,xl2,y+ty);
         }
      }
      xr1 = x + s2x*cx;
      xr2 = gPad->XtoPad(theX[i] + theEXhigh[i]);
      if (xr1 < xr2) {
         if (arrowOpt) {
            arrow.PaintArrow(xr1,y,xr2,y,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(xr1,y,xr2,y);
            if (endLines)  gPad->PaintLine(xr2,y-ty,xr2,y+ty);
         }
      }
      yup1 = y + s2y*cy;
      yup2 = gPad->YtoPad(theY[i] + theEYhigh[i]);
      if (yup2 > gPad->GetUymax()) yup2 =  gPad->GetUymax();
      if (yup2 > yup1) {
         if (arrowOpt) {
            arrow.PaintArrow(x,yup1,x,yup2,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(x,yup1,x,yup2);
            if (endLines)  gPad->PaintLine(x-tx,yup2,x+tx,yup2);
         }
      }
      ylow1 = y - s2y*cy;
      ylow2 = gPad->YtoPad(theY[i] - theEYlow[i]);
      if (ylow2 < gPad->GetUymin()) ylow2 =  gPad->GetUymin();
      if (ylow2 < ylow1) {
         if (arrowOpt) {
            arrow.PaintArrow(x,ylow1,x,ylow2,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(x,ylow1,x,ylow2);
            if (endLines)  gPad->PaintLine(x-tx,ylow2,x+tx,ylow2);
         }
      }
   }
   if (!brackets && !axis) PaintGraphSimple(theGraph, option);
   gPad->ResetBit(TGraph::kClipFrame);

   if (option3) {
      Int_t logx = gPad->GetLogx();
      Int_t logy = gPad->GetLogy();
      gPad->SetLogx(0);
      gPad->SetLogy(0);
      if (option4) PaintGraph(theGraph, 2*theNpoints, xline, yline,"FC");
      else         PaintGraph(theGraph, 2*theNpoints, xline, yline,"F");
      gPad->SetLogx(logx);
      gPad->SetLogy(logy);
      delete [] xline;
      delete [] yline;
   }
}


//_____________________________________________________________________________
void TGraphPainter::PaintGraphBentErrors(TGraph *theGraph, Option_t *option)
{
   // Paint this TGraphBentErrors with its current attributes
   //
   // by default horizonthal and vertical small lines are drawn at
   // the end of the error bars. if option "z" or "Z" is specified,
   // these lines are not drawn.
   //
   // if option contains ">" an arrow is drawn at the end of the error bars
   // if option contains "|>" a full arrow is drawn at the end of the error bars
   // the size of the arrow is set to 2/3 of the marker size.
   //
   // By default, error bars are drawn. If option "X" is specified,
   // the errors are not drawn (TGraph::Paint equivalent).
   //
   // if option "[]" is specified only the end vertical/horizonthal lines
   // of the error bars are drawn. This option is interesting to superimpose
   // systematic errors on top of a graph with statistical errors.
   // if option "2" is specified error rectangles are drawn.
   //
   // if option "3" is specified a filled area is drawn through the end points >
   // the vertical error bars.
   //
   // if option "4" is specified a smoothed filled area is drawn through the end
   // points of the vertical error bars.

   Double_t *xline = 0;
   Double_t *yline = 0;
   Int_t if1 = 0;
   Int_t if2 = 0;

   const Int_t kBASEMARKER=8;
   Double_t s2x, s2y, symbolsize, sbase;
   Double_t x, y, xl1, xl2, xr1, xr2, yup1, yup2, ylow1, ylow2, tx, ty;
   Double_t bxl, bxh, byl, byh;
   static Float_t cxx[11] = {1,1,0.6,0.6,1,1,0.6,0.5,1,0.6,0.6};
   static Float_t cyy[11] = {1,1,1,1,1,1,1,1,1,0.5,0.6};
   Int_t theNpoints = theGraph->GetN();
   Double_t *theX  = theGraph->GetX();
   Double_t *theY  = theGraph->GetY();
   Double_t *theEXlow  = theGraph->GetEXlow();
   Double_t *theEYlow  = theGraph->GetEYlow();
   Double_t *theEXhigh = theGraph->GetEXhigh();
   Double_t *theEYhigh = theGraph->GetEYhigh();

   if (strchr(option,'X') || strchr(option,'x')) {PaintGraphSimple(theGraph, option); return;}
   Bool_t brackets = kFALSE;
   if (strstr(option,"[]")) brackets = kTRUE;
   Bool_t endLines = kTRUE;
   if (strchr(option,'z')) endLines = kFALSE;
   if (strchr(option,'Z')) endLines = kFALSE;
   const char *arrowOpt = 0;
   if (strchr(option,'>'))  arrowOpt = ">";
   if (strstr(option,"|>")) arrowOpt = "|>";

   Bool_t axis = kFALSE;
   if (strchr(option,'a')) axis = kTRUE;
   if (strchr(option,'A')) axis = kTRUE;
   if (axis) PaintGraphSimple(theGraph,option);

   Bool_t option2 = kFALSE;
   Bool_t option3 = kFALSE;
   Bool_t option4 = kFALSE;
   if (strchr(option,'2')) option2 = kTRUE;
   if (strchr(option,'3')) option3 = kTRUE;
   if (strchr(option,'4')) {option3 = kTRUE; option4 = kTRUE;}

   if (option3) {
      xline = new Double_t[2*theNpoints];
      yline = new Double_t[2*theNpoints];
      if (!xline || !yline) {
         Error("Paint", "too many points, out of memory");
         return;
      }
      if1 = 1;
      if2 = 2*theNpoints;
   }

   theGraph->TAttLine::Modify();

   TArrow arrow;
   arrow.SetLineWidth(theGraph->GetLineWidth());
   arrow.SetLineColor(theGraph->GetLineColor());
   arrow.SetFillColor(theGraph->GetFillColor());

   TBox box;
   box.SetLineWidth(theGraph->GetLineWidth());
   box.SetLineColor(theGraph->GetLineColor());
   box.SetFillColor(theGraph->GetFillColor());

   symbolsize  = theGraph->GetMarkerSize();
   sbase       = symbolsize*kBASEMARKER;
   Int_t mark  = theGraph->GetMarkerStyle();
   Double_t cx  = 0;
   Double_t cy  = 0;
   if (mark >= 20 && mark < 31) {
      cx = cxx[mark-20];
      cy = cyy[mark-20];
   }

   // define the offset of the error bars due to the symbol size
   s2x  = gPad->PixeltoX(Int_t(0.5*sbase)) - gPad->PixeltoX(0);
   s2y  =-gPad->PixeltoY(Int_t(0.5*sbase)) + gPad->PixeltoY(0);
   Int_t dxend = Int_t(gStyle->GetEndErrorSize());
   tx   = gPad->PixeltoX(dxend) - gPad->PixeltoX(0);
   ty   =-gPad->PixeltoY(dxend) + gPad->PixeltoY(0);
   Float_t asize = 0.6*symbolsize*kBASEMARKER/gPad->GetWh();

   gPad->SetBit(TGraph::kClipFrame, TestBit(TGraph::kClipFrame));
   for (Int_t i=0;i<theNpoints;i++) {
      x  = gPad->XtoPad(theX[i]);
      y  = gPad->YtoPad(theY[i]);
      bxl = gPad->YtoPad(theY[i]+theEXlow[i]);
      bxh = gPad->YtoPad(theY[i]+theEXhigh[i]);
      byl = gPad->XtoPad(theX[i]+theEYlow[i]);
      byh = gPad->XtoPad(theX[i]+theEYhigh[i]);
      if (x < gPad->GetUxmin()) continue;
      if (x > gPad->GetUxmax()) continue;
      if (y < gPad->GetUymin()) continue;
      if (y > gPad->GetUymax()) continue;

      //  draw the error rectangles
      if (option2) {
         box.PaintBox(gPad->XtoPad(theX[i] - theEXlow[i]),
                      gPad->YtoPad(theY[i] - theEYlow[i]),
                      gPad->XtoPad(theX[i] + theEXhigh[i]),
                      gPad->YtoPad(theY[i] + theEYhigh[i]));
         continue;
      }

      //  keep points for fill area drawing
      if (option3) {
         xline[if1-1] = byh;
         xline[if2-1] = byl;
         yline[if1-1] = gPad->YtoPad(theY[i] + theEYhigh[i]);
         yline[if2-1] = gPad->YtoPad(theY[i] - theEYlow[i]);
         if1++;
         if2--;
         continue;
      }

      xl1 = x - s2x*cx;
      xl2 = gPad->XtoPad(theX[i] - theEXlow[i]);
      if (xl1 > xl2) {
         if (arrowOpt) {
            arrow.PaintArrow(xl1,y,xl2,bxl,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(xl1,y,xl2,bxl);
            if (endLines)  gPad->PaintLine(xl2,bxl-ty,xl2,bxl+ty);
         }
      }
      xr1 = x + s2x*cx;
      xr2 = gPad->XtoPad(theX[i] + theEXhigh[i]);
      if (xr1 < xr2) {
         if (arrowOpt) {
            arrow.PaintArrow(xr1,y,xr2,bxh,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(xr1,y,xr2,bxh);
            if (endLines)  gPad->PaintLine(xr2,bxh-ty,xr2,bxh+ty);
         }
      }
      yup1 = y + s2y*cy;
      yup2 = gPad->YtoPad(theY[i] + theEYhigh[i]);
      if (yup2 > gPad->GetUymax()) yup2 =  gPad->GetUymax();
      if (yup2 > yup1) {
         if (arrowOpt) {
            arrow.PaintArrow(x,yup1,byh,yup2,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(x,yup1,byh,yup2);
            if (endLines)  gPad->PaintLine(byh-tx,yup2,byh+tx,yup2);
         }
      }
      ylow1 = y - s2y*cy;
      ylow2 = gPad->YtoPad(theY[i] - theEYlow[i]);
      if (ylow2 < gPad->GetUymin()) ylow2 =  gPad->GetUymin();
      if (ylow2 < ylow1) {
         if (arrowOpt) {
            arrow.PaintArrow(x,ylow1,byl,ylow2,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(x,ylow1,byl,ylow2);
            if (endLines)  gPad->PaintLine(byl-tx,ylow2,byl+tx,ylow2);
         }
      }
   }
   if (!brackets && !axis) PaintGraphSimple(theGraph, option);
   gPad->ResetBit(TGraph::kClipFrame);

   if (option3) {
      Int_t logx = gPad->GetLogx();
      Int_t logy = gPad->GetLogy();
      gPad->SetLogx(0);
      gPad->SetLogy(0);
      if (option4) PaintGraph(theGraph, 2*theNpoints, xline, yline,"FC");
      else         PaintGraph(theGraph, 2*theNpoints, xline, yline,"F");
      gPad->SetLogx(logx);
      gPad->SetLogy(logy);
      delete [] xline;
      delete [] yline;
   }
}


//______________________________________________________________________________
void TGraphPainter::PaintGraphErrors(TGraph *theGraph, Option_t *option)
{
   // Paint this TGraphErrors with its current attributes
   //
   // by default horizonthal and vertical small lines are drawn at
   // the end of the error bars. if option "z" or "Z" is specified,
   // these lines are not drawn.
   //
   // if option contains ">" an arrow is drawn at the end of the error bars
   // if option contains "|>" a full arrow is drawn at the end of the error bars
   // the size of the arrow is set to 2/3 of the marker size.
   //
   // By default, error bars are drawn. If option "X" is specified,
   // the errors are not drawn (TGraph::Paint equivalent).
   //
   // if option "[]" is specified only the end vertical/horizonthal lines
   // of the error bars are drawn. This option is interesting to superimpose
   // systematic errors on top of a graph with statistical errors.
   //
   // if option "2" is specified error rectangles are drawn.
   //
   // if option "3" is specified a filled area is drawn through the end points of
   // the vertical error bars.
   //
   // if option "4" is specified a smoothed filled area is drawn through the end
   // points of the vertical error bars.
   //
   // Use gStyle->SetErrorX(dx) to control the size of the error along x.
   // set dx = 0 to suppress the error along x.
   //
   // Use gStyle->SetEndErrorSize(np) to control the size of the lines
   // at the end of the error bars (when option 1 is used).
   // By default np=1. (np represents the number of pixels).

   Double_t *xline = 0;
   Double_t *yline = 0;
   Int_t if1 = 0;
   Int_t if2 = 0;

   const Int_t kBASEMARKER=8;
   Double_t s2x, s2y, symbolsize, sbase;
   Double_t x, y, ex, ey, xl1, xl2, xr1, xr2, yup1, yup2, ylow1, ylow2, tx, ty;
   static Float_t cxx[11] = {1,1,0.6,0.6,1,1,0.6,0.5,1,0.6,0.6};
   static Float_t cyy[11] = {1,1,1,1,1,1,1,1,1,0.5,0.6};
   Int_t theNpoints = theGraph->GetN();
   Double_t *theX  = theGraph->GetX();
   Double_t *theY  = theGraph->GetY();
   Double_t *theEX = theGraph->GetEX();
   Double_t *theEY = theGraph->GetEY();

   
   if (strchr(option,'X') || strchr(option,'x')) {PaintGraphSimple(theGraph, option); return;}
   Bool_t brackets = kFALSE;
   if (strstr(option,"[]")) brackets = kTRUE;
   Bool_t endLines = kTRUE;
   if (strchr(option,'z')) endLines = kFALSE;
   if (strchr(option,'Z')) endLines = kFALSE;
   const char *arrowOpt = 0;
   if (strchr(option,'>'))  arrowOpt = ">";
   if (strstr(option,"|>")) arrowOpt = "|>";

   Bool_t axis = kFALSE;
   if (strchr(option,'a')) axis = kTRUE;
   if (strchr(option,'A')) axis = kTRUE;
   if (axis) PaintGraphSimple(theGraph, option);

   Bool_t option2 = kFALSE;
   Bool_t option3 = kFALSE;
   Bool_t option4 = kFALSE;
   if (strchr(option,'2')) option2 = kTRUE;
   if (strchr(option,'3')) option3 = kTRUE;
   if (strchr(option,'4')) {option3 = kTRUE; option4 = kTRUE;}

   if (option3) {
      xline = new Double_t[2*theNpoints];
      yline = new Double_t[2*theNpoints];
      if (!xline || !yline) {
         Error("Paint", "too many points, out of memory");
         return;
      }
      if1 = 1;
      if2 = 2*theNpoints;
   }

   theGraph->TAttLine::Modify();

   TArrow arrow;
   arrow.SetLineWidth(theGraph->GetLineWidth());
   arrow.SetLineColor(theGraph->GetLineColor());
   arrow.SetFillColor(theGraph->GetFillColor());

   TBox box;
   box.SetLineWidth(theGraph->GetLineWidth());
   box.SetLineColor(theGraph->GetLineColor());
   box.SetFillColor(theGraph->GetFillColor());
   box.SetFillStyle(theGraph->GetFillStyle());

   symbolsize  = theGraph->GetMarkerSize();
   sbase       = symbolsize*kBASEMARKER;
   Int_t mark  = theGraph->GetMarkerStyle();
   Double_t cx  = 0;
   Double_t cy  = 0;
   if (mark >= 20 && mark < 31) {
      cx = cxx[mark-20];
      cy = cyy[mark-20];
   }

   //      define the offset of the error bars due to the symbol size
   s2x  = gPad->PixeltoX(Int_t(0.5*sbase)) - gPad->PixeltoX(0);
   s2y  =-gPad->PixeltoY(Int_t(0.5*sbase)) + gPad->PixeltoY(0);
   Int_t dxend = Int_t(gStyle->GetEndErrorSize());
   tx    = gPad->PixeltoX(dxend) - gPad->PixeltoX(0);
   ty    =-gPad->PixeltoY(dxend) + gPad->PixeltoY(0);
   Float_t asize = 0.6*symbolsize*kBASEMARKER/gPad->GetWh();

   gPad->SetBit(TGraph::kClipFrame, TestBit(TGraph::kClipFrame));
   for (Int_t i=0;i<theNpoints;i++) {
      x  = gPad->XtoPad(theX[i]);
      y  = gPad->YtoPad(theY[i]);
      if (option3) {
         if (x < gPad->GetUxmin()) x = gPad->GetUxmin();
         if (x > gPad->GetUxmax()) x = gPad->GetUxmax();
         if (y < gPad->GetUymin()) y = gPad->GetUymin();
         if (y > gPad->GetUymax()) y = gPad->GetUymax();
      } else {
         if (x < gPad->GetUxmin()) continue;
         if (x > gPad->GetUxmax()) continue;
         if (y < gPad->GetUymin()) continue;
         if (y > gPad->GetUymax()) continue;
      }
      ex = theEX[i];
      ey = theEY[i];

      //  draw the error rectangles
      if (option2) {
         box.PaintBox(gPad->XtoPad(theX[i] - ex),
                      gPad->YtoPad(theY[i] - ey),
                      gPad->XtoPad(theX[i] + ex),
                      gPad->YtoPad(theY[i] + ey));
         continue;
      }

      //  keep points for fill area drawing
      if (option3) {
         xline[if1-1] = x;
         xline[if2-1] = x;
         yline[if1-1] = gPad->YtoPad(theY[i] + ey);
         yline[if2-1] = gPad->YtoPad(theY[i] - ey);
         if1++;
         if2--;
         continue;
      }

      xl1 = x - s2x*cx;
      xl2 = gPad->XtoPad(theX[i] - ex);
      if (xl1 > xl2) {
         if (arrowOpt) {
            arrow.PaintArrow(xl1,y,xl2,y,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(xl1,y,xl2,y);
            if (endLines)  gPad->PaintLine(xl2,y-ty,xl2,y+ty);
         }
      }
      xr1 = x + s2x*cx;
      xr2 = gPad->XtoPad(theX[i] + ex);
      if (xr1 < xr2) {
         if (arrowOpt) {
            arrow.PaintArrow(xr1,y,xr2,y,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(xr1,y,xr2,y);
            if (endLines)  gPad->PaintLine(xr2,y-ty,xr2,y+ty);
         }
      }
      yup1 = y + s2y*cy;
      yup2 = gPad->YtoPad(theY[i] + ey);
      if (yup2 > gPad->GetUymax()) yup2 =  gPad->GetUymax();
      if (yup2 > yup1) {
         if (arrowOpt) {
            arrow.PaintArrow(x,yup1,x,yup2,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(x,yup1,x,yup2);
            if (endLines)  gPad->PaintLine(x-tx,yup2,x+tx,yup2);
         }
      }
      ylow1 = y - s2y*cy;
      ylow2 = gPad->YtoPad(theY[i] - ey);
      if (ylow2 < gPad->GetUymin()) ylow2 =  gPad->GetUymin();
      if (ylow2 < ylow1) {
         if (arrowOpt) {
            arrow.PaintArrow(x,ylow1,x,ylow2,asize,arrowOpt);
         } else {
            if (!brackets) gPad->PaintLine(x,ylow1,x,ylow2);
            if (endLines)  gPad->PaintLine(x-tx,ylow2,x+tx,ylow2);
         }
      }
   }
   if (!brackets && !axis) PaintGraphSimple(theGraph, option);
   gPad->ResetBit(TGraph::kClipFrame);

   if (option3) {
      Int_t logx = gPad->GetLogx();
      Int_t logy = gPad->GetLogy();
      gPad->SetLogx(0);
      gPad->SetLogy(0);
         if (option4) PaintGraph(theGraph, 2*theNpoints, xline, yline,"FC");
         else         PaintGraph(theGraph, 2*theNpoints, xline, yline,"F");
      gPad->SetLogx(logx);
      gPad->SetLogy(logy);
      delete [] xline;
      delete [] yline;
   }
}


//______________________________________________________________________________
void TGraphPainter::PaintGraphSimple(TGraph *theGraph, Option_t *option)
{
   // Paint a simple graph, without errors bars.

   if (strstr(option,"H") || strstr(option,"h")) {
      PaintGrapHist(theGraph, theGraph->GetN(), theGraph->GetX(), theGraph->GetY(), option);
   } else {
      PaintGraph(theGraph, theGraph->GetN(), theGraph->GetX(), theGraph->GetY(), option);
   } 
					      
   // Paint associated objects in the list of functions (for instance
   // the fit function).
   TList *functions = theGraph->GetListOfFunctions();
   if (!functions) return;
   TObjOptLink *lnk = (TObjOptLink*)functions->FirstLink();
   TObject *obj;
							     
   while (lnk) {
      obj = lnk->GetObject();
      TVirtualPad *padsave = gPad;
      if (obj->InheritsFrom(TF1::Class())) {
         if (obj->TestBit(TF1::kNotDraw) == 0) obj->Paint("lsame");
      } else  {
         obj->Paint(lnk->GetOption());
      }
      lnk = (TObjOptLink*)lnk->Next();
      padsave->cd();
   }
   return;
}


//______________________________________________________________________________
void TGraphPainter::PaintPolyLineHatches(TGraph *theGraph, Int_t n, const Double_t *x, const Double_t *y)
{
   // Draws a polyline with hatches on one side showing an exclusion
   // zone. x and y are the the vectors holding the polyline and n the
   // number of points in the polyline and w the width of the hatches.
   // w can be negative.
   // This method is not meant to be used directly. It is called
   // automatically according to the line style convention.

   Int_t i,j,nf;
   Double_t w = (theGraph->GetLineWidth()/100)*0.005;

   Double_t *xf = new Double_t[2*n];
   Double_t *yf = new Double_t[2*n];
   Double_t *xt = new Double_t[n];
   Double_t *yt = new Double_t[n];
   Double_t x1, x2, y1, y2, x3, y3, xm, ym, a, a1, a2, a3;

   // Compute the gPad coordinates in TRUE normalized space (NDC)
   Int_t ix1,iy1,ix2,iy2;
   Int_t iw = gPad->GetWw();
   Int_t ih = gPad->GetWh();
   Double_t x1p,y1p,x2p,y2p;
   gPad->GetPadPar(x1p,y1p,x2p,y2p);
   ix1 = (Int_t)(iw*x1p);
   iy1 = (Int_t)(ih*y1p);
   ix2 = (Int_t)(iw*x2p);
   iy2 = (Int_t)(ih*y2p);
   Double_t wndc  = TMath::Min(1.,(Double_t)iw/(Double_t)ih);
   Double_t hndc  = TMath::Min(1.,(Double_t)ih/(Double_t)iw);
   Double_t rh    = hndc/(Double_t)ih;
   Double_t rw    = wndc/(Double_t)iw;
   Double_t x1ndc = (Double_t)ix1*rw;
   Double_t y1ndc = (Double_t)iy1*rh;
   Double_t x2ndc = (Double_t)ix2*rw;
   Double_t y2ndc = (Double_t)iy2*rh;

   // Ratios to convert user space in TRUE normalized space (NDC)
   Double_t rx1,ry1,rx2,ry2;
   gPad->GetRange(rx1,ry1,rx2,ry2);
   Double_t rx = (x2ndc-x1ndc)/(rx2-rx1);
   Double_t ry = (y2ndc-y1ndc)/(ry2-ry1);

   // The first part of the filled area is made of the graph points.
   // Make sure that two adjacent points are different.
   xf[0] = rx*(x[0]-rx1)+x1ndc;
   yf[0] = ry*(y[0]-ry1)+y1ndc;
   nf = 0;
   for (i=1; i<n; i++) {
      if (x[i]==x[i-1] && y[i]==y[i-1]) continue;
      nf++;
      xf[nf] = rx*(x[i]-rx1)+x1ndc;
      yf[nf] = ry*(y[i]-ry1)+y1ndc;
   }

   // For each graph points a shifted points is computed to build up
   // the second part of the filled area. First and last points are
   // treated as special cases, outside of the loop.
   if (xf[1]==xf[0]) {
      a = TMath::PiOver2();
   } else {
      a = TMath::ATan((yf[1]-yf[0])/(xf[1]-xf[0]));
   }
   if (xf[0]<=xf[1]) {
      xt[0] = xf[0]-w*TMath::Sin(a);
      yt[0] = yf[0]+w*TMath::Cos(a);
   } else {
      xt[0] = xf[0]+w*TMath::Sin(a);
      yt[0] = yf[0]-w*TMath::Cos(a);
   }

   if (xf[nf]==xf[nf-1]) {
      a = TMath::PiOver2();
   } else {
      a = TMath::ATan((yf[nf]-yf[nf-1])/(xf[nf]-xf[nf-1]));
   }
   if (xf[nf]>=xf[nf-1]) {
      xt[nf] = xf[nf]-w*TMath::Sin(a);
      yt[nf] = yf[nf]+w*TMath::Cos(a);
   } else {
      xt[nf] = xf[nf]+w*TMath::Sin(a);
      yt[nf] = yf[nf]-w*TMath::Cos(a);
   }

   Double_t xi0,yi0,xi1,yi1,xi2,yi2;
   for (i=1; i<nf; i++) {
      xi0 = xf[i];
      yi0 = yf[i];
      xi1 = xf[i+1];
      yi1 = yf[i+1];
      xi2 = xf[i-1];
      yi2 = yf[i-1];
      if (xi1==xi0) {
         a1 = TMath::PiOver2();
      } else {
         a1  = TMath::ATan((yi1-yi0)/(xi1-xi0));
      }
      if (xi1<xi0) a1 = a1+3.14159;
      if (xi2==xi0) {
         a2 = TMath::PiOver2();
      } else {
         a2  = TMath::ATan((yi0-yi2)/(xi0-xi2));
      }
      if (xi0<xi2) a2 = a2+3.14159;
      x1 = xi0-w*TMath::Sin(a1);
      y1 = yi0+w*TMath::Cos(a1);
      x2 = xi0-w*TMath::Sin(a2);
      y2 = yi0+w*TMath::Cos(a2);
      xm = (x1+x2)*0.5;
      ym = (y1+y2)*0.5;
      if (xm==xi0) {
         a3 = TMath::PiOver2();
      } else {
         a3 = TMath::ATan((ym-yi0)/(xm-xi0));
      }
      x3 = xi0-w*TMath::Sin(a3+1.57079);
      y3 = yi0+w*TMath::Cos(a3+1.57079);
      // Rotate (x3,y3) by PI around (xi0,yi0) if it is not on the (xm,ym) side.
      if ((xm-xi0)*(x3-xi0)<0 && (ym-yi0)*(y3-yi0)<0) {
         x3 = 2*xi0-x3;
         y3 = 2*yi0-y3;
      }
      if ((xm==x1) && (ym==y1)) {
         x3 = xm;
         y3 = ym;
      }
      xt[i] = x3;
      yt[i] = y3;
   }

   // Close the polygon if the first and last points are the same
   if (xf[nf]==xf[0] && yf[nf]==yf[0]) {
      xm = (xt[nf]+xt[0])*0.5;
      ym = (yt[nf]+yt[0])*0.5;
      if (xm==xf[0]) {
         a3 = TMath::PiOver2();
      } else {
         a3 = TMath::ATan((ym-yf[0])/(xm-xf[0]));
      }
      x3 = xf[0]+w*TMath::Sin(a3+1.57079);
      y3 = yf[0]-w*TMath::Cos(a3+1.57079);
      if ((xm-xf[0])*(x3-xf[0])<0 && (ym-yf[0])*(y3-yf[0])<0) {
         x3 = 2*xf[0]-x3;
         y3 = 2*yf[0]-y3;
      }
      xt[nf] = x3;
      xt[0]  = x3;
      yt[nf] = y3;
      yt[0]  = y3;
   }

   // Find the crossing segments and remove the useless ones
   Double_t xc, yc, c1, b1, c2, b2;
   Bool_t cross = kFALSE;
   Int_t nf2 = nf;
   for (i=nf2; i>0; i--) {
      for (j=i-1; j>0; j--) {
         if(xt[i-1]==xt[i] || xt[j-1]==xt[j]) continue;
         c1  = (yt[i-1]-yt[i])/(xt[i-1]-xt[i]);
         b1  = yt[i]-c1*xt[i];
         c2  = (yt[j-1]-yt[j])/(xt[j-1]-xt[j]);
         b2  = yt[j]-c2*xt[j];
         if (c1 != c2) {
            xc = (b2-b1)/(c1-c2);
            yc = c1*xc+b1;
            if (xc>TMath::Min(xt[i],xt[i-1]) && xc<TMath::Max(xt[i],xt[i-1]) &&
                xc>TMath::Min(xt[j],xt[j-1]) && xc<TMath::Max(xt[j],xt[j-1]) &&
                yc>TMath::Min(yt[i],yt[i-1]) && yc<TMath::Max(yt[i],yt[i-1]) &&
                yc>TMath::Min(yt[j],yt[j-1]) && yc<TMath::Max(yt[j],yt[j-1])) {
               nf++; xf[nf] = xt[i]; yf[nf] = yt[i];
               nf++; xf[nf] = xc   ; yf[nf] = yc;
               i = j;
               cross = kTRUE;
               break;
            } else {
               continue;
            }
         } else {
            continue;
         }
      }
      if (!cross) {
         nf++;
         xf[nf] = xt[i];
         yf[nf] = yt[i];
      }
      cross = kFALSE;
   }
   nf++; xf[nf] = xt[0]; yf[nf] = yt[0];

   // NDC to user coordinates
   for (i=0; i<nf+1; i++) {
      xf[i] = (1/rx)*(xf[i]-x1ndc)+rx1;
      yf[i] = (1/ry)*(yf[i]-y1ndc)+ry1;
   }

   // Draw filled area
   gPad->PaintFillArea(nf+1,xf,yf);
   theGraph->TAttLine::Modify(); // In case of PaintFillAreaHatches

   delete [] xf;
   delete [] yf;
   delete [] xt;
   delete [] yt;
}


//______________________________________________________________________________
void TGraphPainter::PaintStats(TGraph *theGraph, TF1 *fit)
{
   //  Paint "stats" box with the fit info

   Int_t dofit;
   TPaveStats *stats  = 0;
   TList *functions = theGraph->GetListOfFunctions();
   TIter next(functions);
   TObject *obj;
   while ((obj = next())) {
      if (obj->InheritsFrom(TPaveStats::Class())) {
         stats = (TPaveStats*)obj;
         break;
      }
   }

   if (stats) dofit  = stats->GetOptFit();
   else       dofit  = gStyle->GetOptFit();

   if (!dofit) fit = 0;
   if (!fit) return;
   if (dofit  == 1) dofit  =  111;
   Int_t nlines = 0;
   Int_t print_fval    = dofit%10;
   Int_t print_ferrors = (dofit/10)%10;
   Int_t print_fchi2   = (dofit/100)%10;
   Int_t print_fprob   = (dofit/1000)%10;
   Int_t nlinesf = print_fval + print_fchi2 + print_fprob;
   if (fit) nlinesf += fit->GetNpar();
   Bool_t done = kFALSE;
   Double_t  statw  = 1.8*gStyle->GetStatW();
   Double_t  stath  = 0.25*(nlines+nlinesf)*gStyle->GetStatH();
   if (stats) {
      stats->Clear();
      done = kTRUE;
   } else {
      stats  = new TPaveStats(
               gStyle->GetStatX()-statw,
               gStyle->GetStatY()-stath,
               gStyle->GetStatX(),
               gStyle->GetStatY(),"brNDC");

      stats->SetParent(functions);
      stats->SetOptFit(dofit);
      stats->SetOptStat(0);
      stats->SetFillColor(gStyle->GetStatColor());
      stats->SetFillStyle(gStyle->GetStatStyle());
      stats->SetBorderSize(gStyle->GetStatBorderSize());
      stats->SetTextFont(gStyle->GetStatFont());
      if (gStyle->GetStatFont()%10 > 2)
         stats->SetTextSize(gStyle->GetStatFontSize());
      stats->SetFitFormat(gStyle->GetFitFormat());
      stats->SetStatFormat(gStyle->GetStatFormat());
      stats->SetName("stats");

      stats->SetTextColor(gStyle->GetStatTextColor());
      stats->SetTextAlign(12);
      stats->SetBit(kCanDelete);
      stats->SetBit(kMustCleanup);
   }

   char t[64];
   char textstats[50];
   Int_t ndf = fit->GetNDF();
   sprintf(textstats,"#chi^{2} / ndf = %s%s / %d","%",stats->GetFitFormat(),ndf);
   sprintf(t,textstats,(Float_t)fit->GetChisquare());
   if (print_fchi2) stats->AddText(t);
   if (print_fprob) {
      sprintf(textstats,"Prob  = %s%s","%",stats->GetFitFormat());
      sprintf(t,textstats,(Float_t)TMath::Prob(fit->GetChisquare(),ndf));
      stats->AddText(t);
   }
   if (print_fval || print_ferrors) {
      for (Int_t ipar=0;ipar<fit->GetNpar();ipar++) {
         if (print_ferrors) {
            sprintf(textstats,"%-8s = %s%s #pm %s%s ",fit->GetParName(ipar),"%",stats->GetFitFormat(),"%",stats->GetFitFormat());
            sprintf(t,textstats,(Float_t)fit->GetParameter(ipar)
                            ,(Float_t)fit->GetParError(ipar));
         } else {
            sprintf(textstats,"%-8s = %s%s ",fit->GetParName(ipar),"%",stats->GetFitFormat());
            sprintf(t,textstats,(Float_t)fit->GetParameter(ipar));
         }
         t[63] = 0;
         stats->AddText(t);
      }
   }

   if (!done) functions->Add(stats);
   stats->Paint();
}

//______________________________________________________________________________
void TGraphPainter::Smooth(TGraph *theGraph, Int_t npoints, Double_t *x, Double_t *y, Int_t drawtype)
{
   // Smooth a curve given by N points.
   //
   //   Underlaying routine for Draw based on the CERN GD3 routine TVIPTE
   //
   //     Author - Marlow etc.   Modified by - P. Ward     Date -  3.10.1973
   //
   //   This routine draws a smooth tangentially continuous curve through
   // the sequence of data points P(I) I=1,N where P(I)=(X(I),Y(I))
   // the curve is approximated by a polygonal arc of short vectors .
   // the data points can represent open curves, P(1) != P(N) or closed
   // curves P(2) == P(N) . If a tangential discontinuity at P(I) is
   // required , then set P(I)=P(I+1) . loops are also allowed .
   //
   // Reference Marlow and Powell,Harwell report No.R.7092.1972
   // MCCONALOGUE,Computer Journal VOL.13,NO4,NOV1970Pp392 6
   //
   // _Input parameters:
   //
   //  npoints   : Number of data points.
   //  x         : Abscissa
   //  y         : Ordinate
   //
   //
   // delta is the accuracy required in constructing the curve.
   // if it is zero then the routine calculates a value other-
   // wise it uses this value. (default is 0.0)

   Int_t i, k, kp, km, npointsMax, banksize, n2, npt;
   Int_t maxiterations, finished;
   Int_t jtype, ktype, closed;
   Double_t sxmin, sxmax, symin, symax;
   Double_t delta;
   Double_t xorg, yorg;
   Double_t ratio_signs, xratio, yratio;
   Int_t flgic, flgis;
   Int_t iw, loptx;
   Double_t p1, p2, p3, p4, p5, p6;
   Double_t w1, w2, w3;
   Double_t a, b, c, r, s, t, z;
   Double_t co, so, ct, st, ctu, stu, xnt;
   Double_t dx1, dy1, dx2, dy2, dk1, dk2;
   Double_t xo, yo, dx, dy, xt, yt;
   Double_t xa, xb, ya, yb;
   Double_t u1, u2, u3, tj;
   Double_t cc, err;
   Double_t sb, sth;
   Double_t wsign, tsquare, tcube;
   c = t = co = so = ct = st = ctu = stu = dx1 = dy1 = dx2 = dy2 = 0;
   xt = yt = xa = xb = ya = yb = u1 = u2 = u3 = tj = sb = 0;

   npointsMax  = npoints*10;
   n2          = npointsMax-2;
   banksize    = n2;

   Double_t *qlx = new Double_t[npointsMax];
   Double_t *qly = new Double_t[npointsMax];
   if (!qlx || !qly) {
      Error("Smooth", "not enough space in memory");
      return;
   }

   //  Decode the type of curve according to
   //  chopt of IGHIST.
   //  ('S', 'SA', 'SA1' ,'XS', 'XSA', or 'XSA1')

   loptx = kFALSE;
   jtype  = (drawtype%1000)-10;
   if (jtype > 0) { ktype = jtype; loptx = kTRUE; }
   else             ktype = drawtype%1000;

   Double_t ruxmin = gPad->GetUxmin();
   Double_t ruymin = gPad->GetUymin();
   if (ktype == 3) {
      xorg = ruxmin;
      yorg = ruymin;
   } else {
      xorg = TMath::Max((Double_t)0,ruxmin);
      yorg = TMath::Min(TMath::Max((Double_t)0,ruymin),gPad->GetUymax());
   }

   maxiterations = 20;
   delta         = 0.00055;

   //       Scale data to the range 0-ratio_signs in X, 0-1 in Y
   //       where ratio_signs is the ratio between the number of changes
   //       of sign in Y divided by the number of changes of sign in X

   sxmin = x[0];
   sxmax = x[0];
   symin = y[0];
   symax = y[0];
   Double_t six   = 1;
   Double_t siy   = 1;
   for (i=1;i<npoints;i++) {
      if (i > 1) {
         if ((x[i]-x[i-1])*(x[i-1]-x[i-2]) < 0) six++;
         if ((y[i]-y[i-1])*(y[i-1]-y[i-2]) < 0) siy++;
      }
      if (x[i] < sxmin) sxmin = x[i];
      if (x[i] > sxmax) sxmax = x[i];
      if (y[i] < symin) symin = y[i];
      if (y[i] > symax) symax = y[i];
   }
   closed = 0;
   Double_t dx1n   = TMath::Abs(x[npoints-1]-x[0]);
   Double_t dy1n   = TMath::Abs(y[npoints-1]-y[0]);
   if (dx1n < 0.01*(sxmax-sxmin) && dy1n < 0.01*(symax-symin))  closed = 1;
   if (sxmin == sxmax) xratio = 1;
   else {
      if (six > 1) ratio_signs = siy/six;
      else         ratio_signs = 20;
      xratio = ratio_signs/(sxmax-sxmin);
   }
   if (symin == symax) yratio = 1;
   else                yratio = 1/(symax-symin);

   qlx[0] = x[0];
   qly[0] = y[0];
   for (i=0;i<npoints;i++) {
      x[i] = (x[i]-sxmin)*xratio;
      y[i] = (y[i]-symin)*yratio;
   }

   //           finished is minus one if we must draw a straight line from P(k-1)
   //           to P(k). finished is one if the last call to IPL has  <  N2
   //           points. finished is zero otherwise. npt counts the X and Y
   //           coordinates in work . When npt=N2 a call to IPL is made.

   finished = 0;
   npt      = 1;
   k        = 1;

   //           Convert coordinates back to original system

   //           Separate the set of data points into arcs P(k-1),P(k).
   //           Calculate the direction cosines. first consider whether
   //           there is a continuous tangent at the endpoints.

   if (!closed) {
      if (x[0] != x[npoints-1] || y[0] != y[npoints-1]) goto L40;
      if (x[npoints-2] == x[npoints-1] && y[npoints-2] == y[npoints-1]) goto L40;
      if (x[0] == x[1] && y[0] == y[1]) goto L40;
   }
   flgic = kFALSE;
   flgis = kTRUE;

   //           flgic is true if the curve is open and false if it is closed.
   //           flgis is true in the main loop, but is false if there is
   //           a deviation from the main loop.

   km = npoints - 1;

   //           Calculate direction cosines at P(1) using P(N-1),P(1),P(2).

   goto L100;
L40:
   flgic = kTRUE;
   flgis = kFALSE;

   //           Skip excessive consecutive equal points.

L50:
   if (k >= npoints) {
      finished = 1;  //*-*-  Prepare to clear out remaining short vectors before returning
      if (npt > 1) goto L310;
      goto L390;
   }
   k++;
   if (x[k-1] == x[k-2] && y[k-1] == y[k-2])  goto L50;
L60:
   km = k-1;
   if (k > npoints) {
      finished = 1;  //*-*-  Prepare to clear out remaining short vectors before returning
      if (npt > 1) goto L310;
      goto L390;
   }
   if (k < npoints) goto L90;
   if (!flgic) { kp = 2; goto L130;}

L80:
   if (flgis) goto L150;

   //           Draw a straight line from P(k-1) to P(k).

   finished = -1;
   goto L170;

   //           Test whether P(k) is a cusp.

L90:
   if (x[k-1] == x[k] && y[k-1] == y[k]) goto L80;
L100:
   kp = k+1;
   goto L130;

   //           Branch if the next section of the curve begins at a cusp.

L110:
   if (!flgis) goto L50;

//*-*-           Carry forward the direction cosines from the previous arc.

L120:
   co = ct;
   so = st;
   k++;
   goto L60;

   //           Calculate the direction cosines at P(k).  If k=1 then
   //           N-1 is used for k-1. If k=N then 2 is used for k+1.
   //           direction cosines at P(k) obtained from P(k-1),P(k),P(k+1).

L130:
   dx1 = x[k-1]  - x[km-1];
   dy1 = y[k-1]  - y[km-1];
   dk1 = dx1*dx1 + dy1*dy1;
   dx2 = x[kp-1] - x[k-1];
   dy2 = y[kp-1] - y[k-1];
   dk2 = dx2*dx2 + dy2*dy2;
   ctu = dx1*dk2 + dx2*dk1;
   stu = dy1*dk2 + dy2*dk1;
   xnt = ctu*ctu + stu*stu;

   //           If both ctu and stu are zero,then default.This can
   //           occur when P(k)=P(k+1). I.E. A loop.

   if (xnt < 1.E-25) {
      ctu = dy1;
      stu =-dx1;
      xnt = dk1;
   }
   //           Normalise direction cosines.

   ct = ctu/TMath::Sqrt(xnt);
   st = stu/TMath::Sqrt(xnt);
   if (flgis) goto L160;

   //           Direction cosines at P(k-1) obtained from P(k-1),P(k),P(k+1).

   w3    = 2*(dx1*dy2-dx2*dy1);
   co    = ctu+w3*dy1;
   so    = stu-w3*dx1;
   xnt   = 1/TMath::Sqrt(co*co+so*so);
   co    = co*xnt;
   so    = so*xnt;
   flgis = kTRUE;
   goto L170;

   //           Direction cosines at P(k) obtained from P(k-2),P(k-1),P(k).

L150:
   w3    = 2*(dx1*dy2-dx2*dy1);
   ct    = ctu-w3*dy2;
   st    = stu+w3*dx2;
   xnt   = 1/TMath::Sqrt(ct*ct+st*st);
   ct    = ct*xnt;
   st    = st*xnt;
   flgis = kFALSE;
   goto L170;
L160:
   if (k <= 1) goto L120;

   //           For the arc between P(k-1) and P(k) with direction cosines co,
   //           so and ct,st respectively, calculate the coefficients of the
   //           parametric cubic represented by X(t) and Y(t) where
   //           X(t)=xa*t**3 + xb*t**2 + co*t + xo
   //           Y(t)=ya*t**3 + yb*t**2 + so*t + yo

L170:
   xo = x[k-2];
   yo = y[k-2];
   dx = x[k-1] - xo;
   dy = y[k-1] - yo;

   //           Initialise the values of X(TI),Y(TI) in xt and yt respectively.

   xt = xo;
   yt = yo;
   if (finished < 0) {  //*-*- Draw a straight line between (xo,yo) and (xt,yt)
      xt += dx;
      yt += dy;
      goto L300;
   }
   c  = dx*dx+dy*dy;
   a  = co+ct;
   b  = so+st;
   r  = dx*a+dy*b;
   t  = c*6/(TMath::Sqrt(r*r+2*(7-co*ct-so*st)*c)+r);
   tsquare = t*t;
   tcube   = t*tsquare;
   xa = (a*t-2*dx)/tcube;
   xb = (3*dx-(co+a)*t)/tsquare;
   ya = (b*t-2*dy)/tcube;
   yb = (3*dy-(so+b)*t)/tsquare;

   //           If the curve is close to a straight line then use a straight
   //           line between (xo,yo) and (xt,yt).

   if (.75*TMath::Max(TMath::Abs(dx*so-dy*co),TMath::Abs(dx*st-dy*ct)) <= delta) {
      finished = -1;
      xt += dx;
      yt += dy;
      goto L300;
   }

   //           Calculate a set of values 0 == t(0).LTCT(1) <  ...  < t(M)=TC
   //           such that polygonal arc joining X(t(J)),Y(t(J)) (J=0,1,..M)
   //           is within the required accuracy of the curve

   tj = 0;
   u1 = ya*xb-yb*xa;
   u2 = yb*co-xb*so;
   u3 = so*xa-ya*co;

   //           Given t(J), calculate t(J+1). The values of X(t(J)),
   //           Y(t(J)) t(J) are contained in xt,yt and tj respectively.

L180:
   s  = t - tj;
   iw = -2;

   //           Define iw here later.

   p1 = (2*u1)*tj-u3;
   p2 = (u1*tj-u3)*3*tj+u2;
   p3 = 3*tj*ya+yb;
   p4 = (p3+yb)*tj+so;
   p5 = 3*tj*xa+xb;
   p6 = (p5+xb)*tj+co;

   //           Test D(tj,THETA). A is set to (Y(tj+s)-Y(tj))/s.b is
   //           set to (X(tj+s)-X(tj))/s.

   cc  = 0.8209285;
   err = 0.1209835;
L190:
   iw -= 2;
L200:
   a   = (s*ya+p3)*s+p4;
   b   = (s*xa+p5)*s+p6;

   //           Set z to PSI(D/delta)-cc.

   w1 = -s*(s*u1+p1);
   w2 = s*s*u1-p2;
   w3 = 1.5*w1+w2;

   //           Set the estimate of (THETA-tj)/s.Then set the numerator
   //           of the expression (EQUATION 4.4)/s. Then set the square
   //           of D(tj,tj+s)/delta. Then replace z by PSI(D/delta)-cc.

   if (w3 > 0) wsign = TMath::Abs(w1);
   else        wsign = -TMath::Abs(w1);
   sth = 0.5+wsign/(3.4*TMath::Abs(w1)+5.2*TMath::Abs(w3));
   z   = s*sth*(s-s*sth)*(w1*sth+w1+w2);
   z   = z*z/((a*a+b*b)*(delta*delta));
   z   = (z+2.642937)*z/((.3715652*z+3.063444)*z+.2441889)-cc;

   //           Branch if z has been calculated

   if (iw > 0) goto L250;
   if (z > err) goto L240;
   goto L220;
L210:
   iw -= 2;
L220:
   if (iw+2 == 0) goto L190;
   if (iw+2 >  0) goto L290;

   //           Last part of arc.

L230:
   xt = x[k-1];
   yt = y[k-1];
   s  = 0;
   goto L300;

   //           z(s). find a value of s where 0 <= s <= sb such that
   //           TMath::Abs(z(s)) < err

L240:
   kp = 0;
   c  = z;
   sb = s;
L250:
   Zero(kp,0,sb,err,s,z,maxiterations);
   if (kp == 2) goto L210;
   if (kp > 2) {
      Error("Smooth", "Attempt to plot outside plot limits");
      goto L230;
   }
   if (iw > 0) goto L200;

   //           Set z=z(s) for s=0.

   if (iw < 0) {
      z  = -cc;
      iw = 0;
      goto L250;
   }

   //           Set z=z(s) for s=sb.

   z  = c;
   iw = 1;
   goto L250;

   //           Update tj,xt and yt.

L290:
   xt = xt + s*b;
   yt = yt + s*a;
   tj = s  + tj;

   //           Convert coordinates to original system

L300:
   qlx[npt] = sxmin + xt/xratio;
   qly[npt] = symin + yt/yratio;
   npt++;

   //           If a fill area must be drawn and if the banks LX and
   //           LY are too small they are enlarged in order to draw
   //           the filled area in one go.

   if (npt < banksize)  goto L320;
   if (drawtype >= 1000 || ktype > 1) {
      Int_t newsize = banksize + n2;
      Double_t *qtemp = new Double_t[banksize];
      for (i=0;i<banksize;i++) qtemp[i] = qlx[i];
      delete [] qlx;
      qlx = new Double_t[newsize];
      for (i=0;i<banksize;i++) qlx[i]   = qtemp[i];
      for (i=0;i<banksize;i++) qtemp[i] = qly[i];
      delete [] qly;
      qly = new Double_t[newsize];
      for (i=0;i<banksize;i++) qly[i] = qtemp[i];
      delete [] qtemp;
      banksize = newsize;
      goto L320;
   }

   //           Draw the graph

L310:
   if (drawtype >= 1000) {
      gPad->PaintFillArea(npt,qlx,qly, "B");
   }
   else {
      if (ktype > 1) {
         if (!loptx) {
            qlx[npt]   = qlx[npt-1];
            qlx[npt+1] = qlx[0];
            qly[npt]   = yorg;
            qly[npt+1] = yorg;
         }
         else {
            qlx[npt]   = xorg;
            qlx[npt+1] = xorg;
            qly[npt]   = qly[npt-1];
            qly[npt+1] = qly[0];
         }
         gPad->PaintFillArea(npt+2,qlx,qly);
      }
      if (TMath::Abs(theGraph->GetLineWidth())>99) PaintPolyLineHatches(theGraph, npt, qlx, qly);
      gPad->PaintPolyLine(npt,qlx,qly);
   }
   npt = 1;
   qlx[0] = sxmin + xt/xratio;
   qly[0] = symin + yt/yratio;
L320:
   if (finished > 0) goto L390;
   if (finished < 0) { finished = 0; goto L110;}
   if (s > 0) goto L180;
   goto L110;

   //           Convert coordinates back to original system

L390:
   for (i=0;i<npoints;i++) {
      x[i] = sxmin + x[i]/xratio;
      y[i] = symin + y[i]/yratio;
   }

   delete [] qlx;
   delete [] qly;
}


//______________________________________________________________________________
void TGraphPainter::Zero(Int_t &k,Double_t AZ,Double_t BZ,Double_t E2,Double_t &X,Double_t &Y
                 ,Int_t maxiterations)
{
   // Find zero of a continuous function.
   //
   //  Underlaying routine for PaintGraph
   // This function finds a real zero of the continuous real
   // function Y(X) in a given interval (A,B). See accompanying
   // notes for details of the argument list and calling sequence

   static Double_t a, b, ya, ytest, y1, x1, h;
   static Int_t j1, it, j3, j2;
   Double_t yb, x2;
   yb = 0;

   //       Calculate Y(X) at X=AZ.
   if (k <= 0) {
      a  = AZ;
      b  = BZ;
      X  = a;
      j1 = 1;
      it = 1;
      k  = j1;
      return;
   }

   //       Test whether Y(X) is sufficiently small.

   if (TMath::Abs(Y) <= E2) { k = 2; return; }

   //       Calculate Y(X) at X=BZ.

   if (j1 == 1) {
      ya = Y;
      X  = b;
      j1 = 2;
      return;
   }
   //       Test whether the signs of Y(AZ) and Y(BZ) are different.
   //       if not, begin the binary subdivision.

   if (j1 != 2) goto L100;
   if (ya*Y < 0) goto L120;
   x1 = a;
   y1 = ya;
   j1 = 3;
   h  = b - a;
   j2 = 1;
   x2 = a + 0.5*h;
   j3 = 1;
   it++;      //*-*-   Check whether (maxiterations) function values have been calculated.
   if (it >= maxiterations) k = j1;
   else                     X = x2;
   return;

   //      Test whether a bracket has been found .
   //      If not,continue the search

L100:
   if (j1 > 3) goto L170;
   if (ya*Y >= 0) {
      if (j3 >= j2) {
         h  = 0.5*h; j2 = 2*j2;
         a  = x1;  ya = y1;  x2 = a + 0.5*h; j3 = 1;
      }
      else {
         a  = X;   ya = Y;   x2 = X + h;     j3++;
      }
      it++;
      if (it >= maxiterations) k = j1;
      else                     X = x2;
      return;
   }

   //       The first bracket has been found.calculate the next X by the
   //       secant method based on the bracket.

L120:
   b  = X;
   yb = Y;
   j1 = 4;
L130:
   if (TMath::Abs(ya) > TMath::Abs(yb)) { x1 = a; y1 = ya; X  = b; Y  = yb; }
   else                                 { x1 = b; y1 = yb; X  = a; Y  = ya; }

   //       Use the secant method based on the function values y1 and Y.
   //       check that x2 is inside the interval (a,b).

L150:
   x2    = X-Y*(X-x1)/(Y-y1);
   x1    = X;
   y1    = Y;
   ytest = 0.5*TMath::Min(TMath::Abs(ya),TMath::Abs(yb));
   if ((x2-a)*(x2-b) < 0) {
      it++;
      if (it >= maxiterations) k = j1;
      else                     X = x2;
      return;
   }

   //       Calculate the next value of X by bisection . Check whether
   //       the maximum accuracy has been achieved.

L160:
   x2    = 0.5*(a+b);
   ytest = 0;
   if ((x2-a)*(x2-b) >= 0) { k = 2;  return; }
   it++;
   if (it >= maxiterations) k = j1;
   else                     X = x2;
   return;


   //       Revise the bracket (a,b).

L170:
   if (j1 != 4) return;
   if (ya*Y < 0) { b  = X; yb = Y; }
   else          { a  = X; ya = Y; }

   //       Use ytest to decide the method for the next value of X.

   if (ytest <= 0) goto L130;
   if (TMath::Abs(Y)-ytest <= 0) goto L150;
   goto L160;
}
