/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

/** Osnola:
IMPORTANT NOTE: some Quickdraw lines/frames can not be "quickly" drawn exactly:
for instance, when PenSize=(1,1), the line from (0,0) to (8,0)
corresponds to the rectangle (0,0)(0,1)(9,1)(9,0), which can only be drawn
 by drawing a rectangle. Drawing a non horizontal/vertical will imply to draw
a polygon, ...
Similarly, drawing the frame of a rectangle (0,0)(0,1)(9,1)(9,0) when PenSize=(1,1),
will imply to draw a rectangle (0.5,0.5)(0.5,8.5)(8.5,8.5)(8.5,0.5) with linewidth=1...

Here, we choose:
- for horizontal/vertical lines and line with length less than five to draw the real line,
- in the other case, we keep the same shape (even if this means some "bad" coordinates)
*/

#include <basegfx/polygon/b2dpolygon.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include "shape.hxx"

namespace PictReaderShapePrivate {
  /** returns an inside rectangle knowing the penSize in order to obtain the ``correct'' position
      when we draw a frame in wide length*/
  static tools::Rectangle contractRectangle(bool drawFrame, tools::Rectangle const &rect, Size const &pSize) {
    if (!drawFrame) return rect;
    tools::Long penSize=(pSize.Width()+pSize.Height())/2;
    if (2*penSize > rect.Right()-rect.Left()) penSize = (rect.Right()-rect.Left()+1)/2;
    if (2*penSize > rect.Bottom()-rect.Top()) penSize = (rect.Bottom()-rect.Top()+1)/2;
    tools::Long const X[2] = { rect.Left()+penSize/2, rect.Right()-(penSize+1)/2 };
    tools::Long const Y[2] = { rect.Top()+penSize/2, rect.Bottom()-(penSize+1)/2 };
    return tools::Rectangle(Point(X[0],Y[0]), Point(X[1], Y[1]));
  }
}

namespace PictReaderShape {
  //--------- draws a horizontal/vertical/small line (by creating a "rectangle/polygon")  ---------
  static bool drawLineHQ(VirtualDevice *dev, Point const &orig, Point const &dest, Size const &pSize) {
    tools::Long dir[2] = { dest.X()-orig.X(), dest.Y()-orig.Y() };
    bool vertic = dir[0] == 0;
    bool horiz = dir[1] == 0;
    if (!horiz && !vertic && dir[0]*dir[0]+dir[1]*dir[1] > 25) return false;

    using namespace basegfx;
    B2DPolygon poly;
    if (horiz || vertic) {
      tools::Long X[2]={ orig.X(), dest.X() }, Y[2] = { orig.Y(), dest.Y() };
      if (horiz) {
    if (X[0] < X[1]) X[1]+=pSize.Width();
    else X[0]+=pSize.Width();
    Y[1] += pSize.Height();
      }
      else  {
    if (Y[0] < Y[1]) Y[1]+=pSize.Height();
    else Y[0]+=pSize.Height();
    X[1] += pSize.Width();
      }
      poly.append(B2DPoint(X[0], Y[0])); poly.append(B2DPoint(X[1], Y[0]));
      poly.append(B2DPoint(X[1], Y[1])); poly.append(B2DPoint(X[0], Y[1]));
      poly.append(B2DPoint(X[0], Y[0]));
    }
    else {
      tools::Long origPt[4][2] = { { orig.X(), orig.Y() }, { orig.X()+pSize.Width(), orig.Y() },
               { orig.X()+pSize.Width(), orig.Y()+pSize.Height() },
               { orig.X(), orig.Y()+pSize.Height() }};
      int origAvoid = dir[0] > 0 ? (dir[1] > 0 ? 2 : 1) : (dir[1] > 0 ? 3 : 0);
      tools::Long destPt[4][2] = { { dest.X(), dest.Y() }, { dest.X()+pSize.Width(), dest.Y() },
               { dest.X()+pSize.Width(), dest.Y()+pSize.Height() },
               { dest.X(), dest.Y()+pSize.Height() }};
      for (int w = origAvoid+1; w < origAvoid+4; w++) {
    int wh = w%4;
    poly.append(B2DPoint(origPt[wh][0], origPt[wh][1]));
      }
      for (int w = origAvoid+3; w < origAvoid+6; w++) {
    int wh = w%4;
    poly.append(B2DPoint(destPt[wh][0], destPt[wh][1]));
      }
      int wh = (origAvoid+1)%4;
      poly.append(B2DPoint(origPt[wh][0], origPt[wh][1]));
    }

    // HACK: here we use the line coloring when drawing the shape
    //       must be changed if other parameter are changed to draw
    //       a line/fill shape
    dev->Push(vcl::PushFlags::LINECOLOR | vcl::PushFlags::FILLCOLOR);
    Color oldLColor = dev->GetLineColor();
    dev->SetFillColor(oldLColor);
    dev->SetLineColor();
    dev->DrawPolygon(poly);
    dev->Pop();
    return true;
  }


  //-------------------- draws a line --------------------

  void drawLine(VirtualDevice *dev, Point const &orig, Point const &dest, Size const &pSize) {
    if (drawLineHQ(dev,orig,dest,pSize)) return;

    tools::Long penSize=(pSize.Width()+pSize.Height())/2;
    tools::Long decal[2] = { pSize.Width()/2, pSize.Height()/2};

    using namespace basegfx;
    B2DPolygon poly;
    poly.append(B2DPoint(double(orig.X()+decal[0]), double(orig.Y()+decal[1])));
    poly.append(B2DPoint(double(dest.X()+decal[0]), double(dest.Y()+decal[1])));
    dev->DrawPolyLine(poly, double(penSize), basegfx::B2DLineJoin::NONE);
  }

  //--------------------  draws a rectangle --------------------
  /* Note(checkme): contradictally with the QuickDraw's reference 3-23, it seems better to consider
     that the frame/content of a rectangle appears inside the given rectangle. Does a conversion
     appear between the pascal functions and the data stored in the file ? */
  void drawRectangle(VirtualDevice *dev, bool drawFrame, tools::Rectangle const &orig, Size const &pSize) {
    int penSize=(pSize.Width()+pSize.Height())/2;
    tools::Rectangle rect = PictReaderShapePrivate::contractRectangle(drawFrame, orig, pSize);
    tools::Long const X[2] = { rect.Left(), rect.Right() };
    tools::Long const Y[2] = { rect.Top(), rect.Bottom() };

    using namespace basegfx;
    B2DPolygon poly;
    poly.append(B2DPoint(X[0], Y[0])); poly.append(B2DPoint(X[1], Y[0]));
    poly.append(B2DPoint(X[1], Y[1])); poly.append(B2DPoint(X[0], Y[1]));
    poly.append(B2DPoint(X[0], Y[0]));

    if (drawFrame)
      dev->DrawPolyLine(poly, double(penSize), basegfx::B2DLineJoin::NONE);
    else
      dev->DrawPolygon(poly);
  }

  //--------------------  draws an ellipse --------------------
  void drawEllipse(VirtualDevice *dev, bool drawFrame, tools::Rectangle const &orig, Size const &pSize) {
    int penSize=(pSize.Width()+pSize.Height())/2;
    tools::Rectangle oval = PictReaderShapePrivate::contractRectangle(drawFrame, orig, pSize);
    using namespace basegfx;
    tools::Long const X[2] = { oval.Left(), oval.Right() };
    tools::Long const Y[2] = { oval.Top(), oval.Bottom() };
    B2DPoint center(0.5*(X[1]+X[0]), 0.5*(Y[1]+Y[0]));
    B2DPolygon poly = basegfx::utils::createPolygonFromEllipse(center, 0.5*(X[1]-X[0]), 0.5*(Y[1]-Y[0]));
    if (drawFrame)
      dev->DrawPolyLine(poly, double(penSize), basegfx::B2DLineJoin::NONE);
    else
      dev->DrawPolygon(poly);
  }

  //--------------------  draws an arc/pie --------------------
  void drawArc(VirtualDevice *dev, bool drawFrame, tools::Rectangle const &orig, const double& angle1, const double& angle2, Size const &pSize) {
    int penSize=(pSize.Width()+pSize.Height())/2;
    tools::Rectangle arc = PictReaderShapePrivate::contractRectangle(drawFrame, orig, pSize);
    using namespace basegfx;

    // pict angle are CW with 0 at twelve o'clock (with Y-axis inverted)...
    double angl1 = angle1-M_PI_2;
    double angl2 = angle2-M_PI_2;
    tools::Long const X[2] = { arc.Left(), arc.Right() };
    tools::Long const Y[2] = { arc.Top(), arc.Bottom() };
    B2DPoint center(0.5*(X[1]+X[0]), 0.5*(Y[1]+Y[0]));

    // We must have angl1 between 0 and 2PI
    while (angl1 < 0.0) { angl1 += 2 * M_PI; angl2 += 2 * M_PI; }
    while (angl1 >= 2 * M_PI) { angl1  -= 2 * M_PI; angl2 -= 2 * M_PI; }

    // if this happen, we want a complete circle
    // so we set angl2 slightly less than angl1
    if (angl2 >= angl1 + 2 * M_PI) angl2 = angl1-0.001;

    // We must have angl2 between 0 and 2PI
    while (angl2 < 0.0) angl2 += 2 * M_PI;
    while (angl2 >= 2 * M_PI) angl2 -= 2 * M_PI;

    B2DPolygon poly = basegfx::utils::createPolygonFromEllipseSegment(center, 0.5*(X[1]-X[0]), 0.5*(Y[1]-Y[0]), angl1, angl2);
    if (drawFrame)
      dev->DrawPolyLine(poly, double(penSize), basegfx::B2DLineJoin::NONE);
    else {
      // adds circle's center
      poly.append(center);
      dev->DrawPolygon(poly);
    }
  }
  //--------------------  draws a rectangle with round corner --------------------
  void drawRoundRectangle(VirtualDevice *dev, bool drawFrame, tools::Rectangle const &orig, Size const &ovalSize, Size const &pSize) {
    int penSize=(pSize.Width()+pSize.Height())/2;
    tools::Rectangle oval = PictReaderShapePrivate::contractRectangle(drawFrame, orig, pSize);
    int ovalW=ovalSize.Width(), ovalH=ovalSize.Height();
    using namespace basegfx;
    tools::Long const X[2] = { oval.Left(), oval.Right() };
    tools::Long const Y[2] = { oval.Top(), oval.Bottom() };
    tools::Long width = X[1] - X[0];
    tools::Long height = Y[1] - Y[0];
    if (ovalW > width) ovalW = static_cast< int >( width );
    if (ovalH > height) ovalH = static_cast< int >( height );

    B2DRectangle rect(B2DPoint(X[0],Y[0]), B2DPoint(X[1],Y[1]));
    B2DPolygon poly = basegfx::utils::createPolygonFromRect(rect, (width != 0.0) ? ovalW/width : 0.0, (height != 0.0) ? ovalH/height : 0.0);

    if (drawFrame)
      dev->DrawPolyLine(poly, double(penSize), basegfx::B2DLineJoin::NONE);
    else
      dev->DrawPolygon(poly);
  }

  //--------------------  draws a polygon --------------------
void drawPolygon(VirtualDevice *dev, bool drawFrame, tools::Polygon const &orig, Size const &pSize) {
    int penSize=(pSize.Width()+pSize.Height())/2;
    tools::Long decalTL[2] = {0, 0}, decalBR[2] = { pSize.Width(), pSize.Height()};
    if (drawFrame) {
      decalTL[0] += penSize/2; decalTL[1] += penSize/2;
      decalBR[0] -= (penSize+1)/2; decalBR[1] -= (penSize+1)/2;
    }
    // Quickdraw Drawing Reference 3-82: the pen size is only used for frame
    else decalBR[0] = decalBR[1] = 0;


    int numPt = orig.GetSize();
    if (numPt <= 1) return;

    // we compute a barycenter of the point to define the extended direction of each point
    double bary[2] = { 0.0, 0.0 };
    for (int i = 0; i < numPt; i++) {
      Point const &pt = orig.GetPoint(i);
      bary[0] += double(pt.X()); bary[1] += double(pt.Y());
    }
    bary[0]/=double(numPt); bary[1]/=double(numPt);

    using namespace basegfx;
    B2DPolygon poly;
    poly.reserve(numPt);
    // Note: a polygon can be open, so we must not close it when we draw the frame
    for (int i = 0; i < numPt; i++) {
      Point const &pt = orig.GetPoint(i);
      double x = (double(pt.X()) < bary[0]) ? pt.X()+decalTL[0] : pt.X()+decalBR[0];
      double y = (double(pt.Y()) < bary[1]) ? pt.Y()+decalTL[1] : pt.Y()+decalBR[1];
      poly.append(B2DPoint(x, y));
    }
    if (drawFrame)
      dev->DrawPolyLine(poly, double(penSize), basegfx::B2DLineJoin::NONE);
    else
      dev->DrawPolygon(poly);
  }


}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
