/*=========================================================================
 *
 * Copyright (c) 2014 The Regents of the University of California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *=========================================================================*/

/** @file vtkSVCheckRotation.cxx
 *  @brief This implements the vtkSVCheckRotation filter as a class
 *
 *  @author Adam Updegrove
 *  @author updega2@gmail.com
 *  @author UC Berkeley
 *  @author shaddenlab.berkeley.edu
 */

#include "vtkSVCheckRotation.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkCenterOfMass.h"
#include "vtkCleanPolyData.h"
#include "vtkFloatArray.h"
#include "vtkGradientFilter.h"
#include "vtkMath.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPointDataToCellData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkPolyDataNormals.h"
#include "vtkSmartPointer.h"
#include "vtkSVGeneralUtils.h"
#include "vtkSVGlobals.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkTriangle.h"
#include "vtkXMLPolyDataWriter.h"

#include <iostream>
#include <cmath>

//---------------------------------------------------------------------------
//vtkCxxRevisionMacro(vtkSVCheckRotation, "$Revision: 0.0 $");
vtkStandardNewMacro(vtkSVCheckRotation);


//---------------------------------------------------------------------------
vtkSVCheckRotation::vtkSVCheckRotation()
{
  this->SetNumberOfInputPorts(2);

  this->Verbose = 1;
  this->CellId  = 0;

  this->SourcePd = vtkPolyData::New();
  this->TargetPd = vtkPolyData::New();
  this->MappedPd = vtkPolyData::New();

  this->OriginalPd = NULL;
}

//---------------------------------------------------------------------------
vtkSVCheckRotation::~vtkSVCheckRotation()
{
  if (this->SourcePd != NULL)
  {
    this->SourcePd->Delete();
  }
  if (this->TargetPd != NULL)
  {
    this->TargetPd->Delete();
  }
  if (this->MappedPd != NULL)
  {
    this->MappedPd->Delete();
  }
}

//---------------------------------------------------------------------------
void vtkSVCheckRotation::PrintSelf(ostream& os, vtkIndent indent)
{
}

// Generate Separated Surfaces with Region ID Numbers
//---------------------------------------------------------------------------
int vtkSVCheckRotation::RequestData(
                                 vtkInformation *vtkNotUsed(request),
                                 vtkInformationVector **inputVector,
                                 vtkInformationVector *outputVector)
{
  // get the input and output
  vtkPolyData *input1 = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData *input2 = vtkPolyData::GetData(inputVector[1]);
  vtkPolyData *output = vtkPolyData::GetData(outputVector);

  //Copy the input to operate on
  this->SourcePd->DeepCopy(input1);
  this->TargetPd->DeepCopy(input2);

  vtkIdType numSourcePolys = this->SourcePd->GetNumberOfPolys();
  //Check the input to make sure it is there
  if (numSourcePolys < 1)
  {
    vtkDebugMacro("No input!");
    return SV_ERROR;
  }

  if (this->MoveCenters() != SV_OK)
  {
    return SV_ERROR;
  }

  if (this->FindAndCheckRotation() != SV_OK)
  {
    return SV_ERROR;
  }

  if (this->OriginalPd != NULL)
  {
    if (this->CheckAnglesWithOriginal() != SV_OK)
    {
      return SV_ERROR;
    }
  }

  output->DeepCopy(this->MappedPd);
  return SV_OK;
}

//---------------------------------------------------------------------------
/**
 * @brief
 * @param *pd
 * @return
 */
int vtkSVCheckRotation::MoveCenters()
{
  double sourceCenter[3], targetCenter[3];
  vtkSVGeneralUtils::ComputeMassCenter(this->SourcePd, sourceCenter);
  vtkSVGeneralUtils::ComputeMassCenter(this->TargetPd, targetCenter);

  int numPts = this->SourcePd->GetNumberOfPoints();
  for (int i=0; i<numPts; i++)
  {
    double srcPt[3], targPt[3];
    this->SourcePd->GetPoint(i, srcPt);
    this->TargetPd->GetPoint(i, targPt);
    double newSrcPt[3], newTargPt[3];
    vtkMath::Subtract(srcPt, sourceCenter, newSrcPt);
    vtkMath::Subtract(targPt, targetCenter, newTargPt);
    this->SourcePd->GetPoints()->SetPoint(i, newSrcPt);
    this->TargetPd->GetPoints()->SetPoint(i, newTargPt);
  }

  return SV_OK;
}

//---------------------------------------------------------------------------
/**
 * @brief
 * @param *pd
 * @return
 */
int vtkSVCheckRotation::FindAndCheckRotation()
{
  vtkIdType npts, *pts;
  vtkIdType targNpts, *targPts;
  this->SourcePd->GetCellPoints(this->CellId, npts, pts);
  this->TargetPd->GetCellPoints(this->CellId, targNpts, targPts);
  double allSrcPts[3][3], allTargPts[3][3];
  this->SourcePd->GetPoint(pts[0], allSrcPts[0]);
  this->TargetPd->GetPoint(targPts[0], allTargPts[0]);
  this->TargetPd->GetPoint(targPts[1], allTargPts[1]);
  this->TargetPd->GetPoint(targPts[2], allTargPts[2]);
  double sourceCenter[3], targetCenter[3];
  vtkSVGeneralUtils::ComputeMassCenter(this->SourcePd, sourceCenter);
  vtkSVGeneralUtils::ComputeMassCenter(this->TargetPd, targetCenter);

  double vec0[3], vec1[3], tmp0[3], tmp1[3], vec2[3], vec3[3], vec4[3], vec5[3];
  vtkMath::Subtract(allSrcPts[0], sourceCenter, vec0);
  vtkMath::Subtract(allTargPts[0], targetCenter, vec1);
  vtkMath::Subtract(allTargPts[1], targetCenter, tmp1);
  vtkMath::Cross(vec1, tmp1, vec3);
  vtkMath::Cross(vec1, vec3, vec5);

  double rotMatrix0[16], rotMatrix1[16], rotMatrix2[16];
  vtkSVGeneralUtils::GetRotationMatrix(vec0, vec1, rotMatrix0);

  this->MappedPd->DeepCopy(this->SourcePd);
  vtkSVGeneralUtils::ApplyRotationMatrix(this->MappedPd, rotMatrix0);

  this->MappedPd->GetPoint(pts[0], allSrcPts[0]);
  this->MappedPd->GetPoint(pts[1], allSrcPts[1]);
  vtkSVGeneralUtils::ComputeMassCenter(this->MappedPd, sourceCenter);
  vtkMath::Subtract(allSrcPts[0], sourceCenter, vec0);
  vtkMath::Subtract(allSrcPts[1], sourceCenter, tmp0);
  vtkMath::Cross(vec0, tmp0, vec2);
  vtkSVGeneralUtils::GetRotationMatrix(vec2, vec3, rotMatrix1);
  vtkSVGeneralUtils::ApplyRotationMatrix(this->MappedPd, rotMatrix1);

  int numPts = this->SourcePd->GetNumberOfPoints();
  double avgDist = 0.0;
  double maxDist = 0.0;
  double minDist = 1.0e8;
  for (int i=0; i<numPts; i++)
  {
    double srcPt[3], targPt[3], diff[3];;
    this->MappedPd->GetPoint(i, srcPt);
    this->TargetPd->GetPoint(i, targPt);
    for (int j=0; j<3; j++)
    {
      diff[j] = srcPt[j] - targPt[j];
    }
    double ptDist = sqrt(pow(diff[0], 2.0) + pow(diff[1], 2.0) + pow(diff[2], 2.0));
    avgDist += ptDist;
    if (ptDist > maxDist)
      maxDist = ptDist;
    if (ptDist < minDist)
      minDist = ptDist;
  }
  avgDist = avgDist / numPts;
  fprintf(stdout, "Average point distance is: %.8f\n",avgDist);
  fprintf(stdout, "Minimum point distance is: %.8f\n",minDist);
  fprintf(stdout, "Maximum point distance is: %.8f\n",maxDist);

  return SV_OK;
}

//---------------------------------------------------------------------------
/**
 * @brief
 * @param *pd
 * @return
 */
int vtkSVCheckRotation::CheckAnglesWithOriginal()
{
  if (this->MatchPointOrder() != SV_OK)
  {
    return SV_ERROR;
  }
  vtkNew(vtkFloatArray, mappedS2Angles);
  if (vtkSVGeneralUtils::GetPolyDataAngles(this->MappedPd, mappedS2Angles) != SV_OK)
  {
    return SV_ERROR;
  }

  vtkNew(vtkFloatArray, targetS2Angles);
  if (vtkSVGeneralUtils::GetPolyDataAngles(this->TargetPd, targetS2Angles) != SV_OK)
  {
    return SV_ERROR;
  }

  vtkNew(vtkFloatArray, originalPdAngles);
  if (vtkSVGeneralUtils::GetPolyDataAngles(this->OriginalPd, originalPdAngles) != SV_OK)
  {
    return SV_ERROR;
  }

  int numCells = this->OriginalPd->GetNumberOfCells();
  double avgMAng = 0.0;
  double maxMAng = 0.0;
  double minMAng = 1.0e8;
  double avgTAng = 0.0;
  double maxTAng = 0.0;
  double minTAng = 1.0e8;
  for (int i=0; i<numCells; i++)
  {
    double mappedAngs[3], targetAngs[3], originalAngs[3];
    mappedS2Angles->GetTuple(i, mappedAngs);
    targetS2Angles->GetTuple(i, targetAngs);
    originalPdAngles->GetTuple(i, originalAngs);

    for (int j=0; j<3; j++)
    {
      double mDiff = fabs(mappedAngs[j] - originalAngs[j]);
      double tDiff = fabs(targetAngs[j] - originalAngs[j]);
      fprintf(stdout,"| Target Angle | %d | %16.8f |\n", 3*i+j, mappedAngs[j] - originalAngs[j]);
      avgMAng += mDiff;
      avgTAng += tDiff;

      if (mDiff < minMAng)
        minMAng = mDiff;
      if (tDiff < minTAng)
        minTAng = tDiff;

      if (mDiff > maxMAng)
        maxMAng = mDiff;
      if (tDiff > maxTAng)
        maxTAng = tDiff;
    }
  }

  avgMAng = avgMAng / (numCells *3);
  avgTAng = avgTAng / (numCells *3);
  fprintf(stdout, "Average angle difference between source and original is: %.8f\n",avgMAng);
  fprintf(stdout, "Minimum angle difference between source and original is: %.8f\n",minMAng);
  fprintf(stdout, "Maximum angle difference between source and original is: %.8f\n",maxMAng);
  fprintf(stdout, "-----------------------------------------------------------------------\n");
  fprintf(stdout, "Average angle difference between target and original is: %.8f\n",avgTAng);
  fprintf(stdout, "Minimum angle difference between target and original is: %.8f\n",minTAng);
  fprintf(stdout, "Maximum angle difference between target and original is: %.8f\n",maxTAng);

  return SV_OK;
}

//---------------------------------------------------------------------------
/**
 * @brief
 * @param *pd
 * @return
 */
int vtkSVCheckRotation::MatchPointOrder()
{
  int numCells = this->OriginalPd->GetNumberOfCells();

  for (int i=0; i<numCells; i++)
  {
    vtkIdType dnpts, *dpts;
    this->OriginalPd->GetCellPoints(i, dnpts, dpts);
    vtkIdType npts, *pts;
    this->MappedPd->GetCellPoints(i, npts, pts);

    this->OriginalPd->ReplaceCell(i, npts, pts);
  }
  return SV_OK;
}
