/*=========================================================================
 *
 * Copyright (c) 2014-2015 The Regents of the University of California.
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

#include "vtkSVGroupsSegmenter.h"

#include "vtkSVCenterlineBranchSplitter.h"
#include "vtkSVGeneralUtils.h"
#include "vtkSVGlobals.h"
#include "vtkSVPolycubeGenerator.h"
#include "vtkSVParameterizeSurfaceOnPolycube.h"
#include "vtkSVParameterizeVolumeOnPolycube.h"
#include "vtkSVPassDataArray.h"
#include "vtkSVSurfaceCenterlineAttributesPasser.h"
#include "vtkSVSurfaceCenterlineGrouper.h"
#include "vtkSVSurfaceCuboidPatcher.h"

#include "vtkExecutive.h"
#include "vtkErrorCode.h"
#include "vtkCellArray.h"
#include "vtkCellLocator.h"
#include "vtkConnectivityFilter.h"
#include "vtkPolyLine.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPointLocator.h"
#include "vtkCellData.h"
#include "vtkCleanPolyData.h"
#include "vtkSmartPointer.h"
#include "vtkSmartPointer.h"
#include "vtkSplineFilter.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPolyDataNormals.h"
#include "vtkStructuredGridGeometryFilter.h"
#include "vtkUnstructuredGrid.h"
#include "vtkVersion.h"

#include "vtkvmtkMergeCenterlines.h"
#include "vtkvmtkPolyDataCenterlineGroupsClipper.h"

#include <algorithm>

// ----------------------
// StandardNewMacro
// ----------------------
vtkStandardNewMacro(vtkSVGroupsSegmenter);

// ----------------------
// Constructor
// ----------------------
vtkSVGroupsSegmenter::vtkSVGroupsSegmenter()
{
  this->WorkPd = vtkPolyData::New();
  this->MergedCenterlines = vtkPolyData::New();
  this->PolycubePd = vtkPolyData::New();
  this->GraphPd = vtkPolyData::New();
  this->Centerlines = NULL;

  this->PolycubeUg   = vtkUnstructuredGrid::New();
  this->FinalHexMesh = vtkUnstructuredGrid::New();

  this->CenterlineGroupIdsArrayName = NULL;
  this->CenterlineRadiusArrayName = NULL;
  this->GroupIdsArrayName = NULL;
  this->BlankingArrayName = NULL;

  this->UseVmtkClipping = 0;
  this->EnforceBoundaryDirections = 1;
  this->CutoffRadiusFactor = VTK_SV_LARGE_DOUBLE;
  this->ClipValue = 0.0;
  this->UseRadiusInformation = 1;

  this->PolycubeDivisions = 5;
  this->PolycubeUnitLength = 0.0;

  this->NormalsWeighting = 0.8;
  this->IsVasculature = 1;
  this->NumberOfCenterlineRemovePts = 3;
  this->BoundaryEnforceFactor = 1;

  this->UseAbsoluteMergeDistance = 0;
  this->RadiusMergeRatio = 0.5;
  this->MergeDistance = 0.1;
}

// ----------------------
// Destructor
// ----------------------
vtkSVGroupsSegmenter::~vtkSVGroupsSegmenter()
{
  if (this->WorkPd != NULL)
  {
    this->WorkPd->Delete();
    this->WorkPd = NULL;
  }
  if (this->MergedCenterlines != NULL)
  {
    this->MergedCenterlines->Delete();
    this->MergedCenterlines = NULL;
  }
  if (this->Centerlines != NULL)
  {
    this->Centerlines->Delete();
    this->Centerlines = NULL;
  }
  if (this->PolycubePd != NULL)
  {
    this->PolycubePd->Delete();
    this->PolycubePd = NULL;
  }
  if (this->GraphPd != NULL)
  {
    this->GraphPd->Delete();
    this->GraphPd = NULL;
  }

  if (this->PolycubeUg != NULL)
  {
    this->PolycubeUg->Delete();
    this->PolycubeUg = NULL;
  }

  if (this->FinalHexMesh != NULL)
  {
    this->FinalHexMesh->Delete();
    this->FinalHexMesh = NULL;
  }

  if (this->CenterlineGroupIdsArrayName != NULL)
  {
    delete [] this->CenterlineGroupIdsArrayName;
    this->CenterlineGroupIdsArrayName = NULL;
  }

  if (this->CenterlineRadiusArrayName != NULL)
  {
    delete [] this->CenterlineRadiusArrayName;
    this->CenterlineRadiusArrayName = NULL;
  }

  if (this->GroupIdsArrayName != NULL)
  {
    delete [] this->GroupIdsArrayName;
    this->GroupIdsArrayName = NULL;
  }

  if (this->BlankingArrayName != NULL)
  {
    delete [] this->BlankingArrayName;
    this->BlankingArrayName = NULL;
  }
}

// ----------------------
// RequestData
// ----------------------
int vtkSVGroupsSegmenter::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **inputVector,
  vtkInformationVector *outputVector)
{
  // get the input and output
  vtkPolyData *input = vtkPolyData::GetData(inputVector[0]);
  vtkPolyData *output = vtkPolyData::GetData(outputVector);

  this->WorkPd->DeepCopy(input);

  // Prep work for filter
  if (this->PrepFilter() != SV_OK)
  {
    vtkErrorMacro("Prep of filter failed");
    output->DeepCopy(input);
    this->SetErrorCode(vtkErrorCode::UserError + 1);
    return SV_ERROR;
  }

  // Run the filter
  if (this->RunFilter() != SV_OK)
  {
    vtkErrorMacro("Filter failed");
    output->DeepCopy(this->WorkPd);
    this->SetErrorCode(vtkErrorCode::UserError + 2);
    return SV_ERROR;
  }

  output->DeepCopy(this->WorkPd);

  return SV_OK;
}

// ----------------------
// PrepFilter
// ----------------------
int vtkSVGroupsSegmenter::PrepFilter()
{
  if (!this->Centerlines)
  {
    vtkErrorMacro(<< "Centerlines not set.");
    return SV_ERROR;
  }

  if (this->MergeCenterlines() != SV_OK)
  {
    vtkErrorMacro("Problem merging centerlines");
    return SV_ERROR;
  }

  if (!this->CenterlineGroupIdsArrayName)
  {
    vtkDebugMacro("Centerline GroupIds Array Name not given, setting to GroupIds");
    this->CenterlineGroupIdsArrayName = new char[strlen("GroupIds") + 1];
    strcpy(this->CenterlineGroupIdsArrayName, "GroupIds");
  }

  if (!this->GroupIdsArrayName)
  {
    vtkDebugMacro("GroupIds Array Name not given, setting to GroupIds");
    this->GroupIdsArrayName = new char[strlen("GroupIds") + 1];
    strcpy(this->GroupIdsArrayName, "GroupIds");
  }

  if (vtkSVGeneralUtils::CheckArrayExists(this->Centerlines, 1, this->CenterlineGroupIdsArrayName) != SV_OK)
  {
    vtkErrorMacro(<< "CenterlineGroupIdsArray with name specified does not exist");
    return SV_OK;
  }
  if (vtkSVGeneralUtils::CheckArrayExists(this->MergedCenterlines, 1, this->CenterlineGroupIdsArrayName) != SV_OK)
  {
    vtkErrorMacro(<< "CenterlineGroupIdsArray with name specified does not exist");
    return SV_OK;
  }

  if (!this->BlankingArrayName)
  {
    vtkDebugMacro("Blanking Array Name not given, setting to Blanking");
    this->BlankingArrayName = new char[strlen("Blanking") + 1];
    strcpy(this->BlankingArrayName, "Blanking");
  }

  if (vtkSVGeneralUtils::CheckArrayExists(this->Centerlines, 1, this->BlankingArrayName) != SV_OK)
  {
    vtkErrorMacro(<< "BlankingArrayName with name specified does not exist");
    return SV_ERROR;
  }

  if (!this->CenterlineRadiusArrayName)
  {
    vtkDebugMacro("Centerline radius Array Name not given, setting to MaximumInscribedSphereRadius");
    this->CenterlineRadiusArrayName = new char[strlen("MaximumInscribedSphereRadius") + 1];
    strcpy(this->CenterlineRadiusArrayName, "MaximumInscribedSphereRadius");
  }

  if (!this->Centerlines->GetPointData()->GetArray(this->CenterlineRadiusArrayName))
  {
    vtkErrorMacro(<< "CenterlineRadiusArray with name specified does not exist");
    return SV_ERROR;
  }

  vtkNew(vtkSVPolycubeGenerator, polycuber);
  polycuber->SetInputData(this->MergedCenterlines);
  polycuber->SetCenterlineGroupIdsArrayName(this->CenterlineGroupIdsArrayName);
  polycuber->SetCenterlineRadiusArrayName(this->CenterlineRadiusArrayName);
  polycuber->Update();

  this->PolycubePd->DeepCopy(polycuber->GetOutput());
  this->PolycubeUg->DeepCopy(polycuber->GetVolumePolycubeUg());
  this->GraphPd->DeepCopy(polycuber->GetGraphPd());
  this->PolycubeUnitLength = polycuber->GetPolycubeUnitLength();
  this->PolycubeDivisions  = polycuber->GetPolycubeDivisions();
  // Need to copy lines from centerline graph because data pertaining to
  // local coordinate systems are added onto the centerlines during processing
  this->MergedCenterlines->DeepCopy(polycuber->GetCenterlineGraph()->Lines);
  // TODO: Adding of local coordinates as own filter

  if (this->PolycubePd->GetNumberOfCells() == 0)
  {
    vtkErrorMacro("Polycube generation failed");
    return SV_ERROR;
  }

  return SV_OK;
}

// ----------------------
// RunFilter
// ----------------------
int vtkSVGroupsSegmenter::RunFilter()
{
  // Generate normals just in case they don't exist
  vtkNew(vtkPolyDataNormals, normaler);
  normaler->SetInputData(this->WorkPd);
  normaler->ComputePointNormalsOff();
  normaler->ComputeCellNormalsOn();
  normaler->SplittingOff();
  normaler->Update();

  this->WorkPd->DeepCopy(normaler->GetOutput());
  this->WorkPd->BuildLinks();
  vtkDataArray *normalsArray =
    this->WorkPd->GetCellData()->GetArray("Normals");

  int stopCellNumber = ceil(this->WorkPd->GetNumberOfCells()*0.0001);

  if  (this->UseVmtkClipping)
  {
    vtkNew(vtkSplineFilter, resampler);
    resampler->SetInputData(this->Centerlines);
    //resampler->SetInputData(this->MergedCenterlines);
    resampler->SetSubdivideToLength();
    resampler->SetLength(this->Centerlines->GetLength()/100.);
    resampler->Update();

    vtkNew(vtkvmtkPolyDataCenterlineGroupsClipper, branchClipper);
    branchClipper->SetInputData(this->WorkPd);
    branchClipper->SetCenterlines(resampler->GetOutput());
    branchClipper->SetGroupIdsArrayName(this->GroupIdsArrayName);
    branchClipper->SetCenterlineGroupIdsArrayName(this->CenterlineGroupIdsArrayName);
    branchClipper->SetCenterlineRadiusArrayName(this->CenterlineRadiusArrayName);
    branchClipper->SetBlankingArrayName(this->BlankingArrayName);
    branchClipper->SetCutoffRadiusFactor(this->CutoffRadiusFactor);
    branchClipper->SetClipValue(this->ClipValue);
    //branchClipper->SetUseRadiusInformation(this->UseRadiusInformation);
    branchClipper->SetUseRadiusInformation(0);
    branchClipper->SetClipAllCenterlineGroupIds(1);
    branchClipper->Update();

    vtkNew(vtkSVPassDataArray, dataPasser);
    dataPasser->SetInputData(0, branchClipper->GetOutput());
    dataPasser->SetInputData(1, this->WorkPd);
    dataPasser->SetPassArrayName(this->GroupIdsArrayName);
    dataPasser->SetPassDataIsCellData(0);
    dataPasser->SetPassDataToCellData(1);
    dataPasser->Update();

    vtkNew(vtkSVSurfaceCenterlineGrouper, grouper);
    grouper->SetInputData(dataPasser->GetOutput());
    grouper->SetPolycubePd(this->PolycubePd);
    grouper->SetMergedCenterlines(this->MergedCenterlines);
    grouper->SetUseRadiusInformation(this->UseRadiusInformation);
    grouper->SetCenterlineRadiusArrayName(this->CenterlineRadiusArrayName);
    grouper->SetCenterlineGroupIdsArrayName(this->CenterlineGroupIdsArrayName);
    grouper->SetCenterlineIdsArrayName("CenterlineIds");
    grouper->SetGroupIdsArrayName(this->GroupIdsArrayName);
    grouper->SetTractIdsArrayName("TractIds");
    grouper->GroupSurfaceOn();
    grouper->EnforceCenterlinesConnectivityOn();
    grouper->EnforcePolycubeConnectivityOn();
    grouper->Update();

    this->WorkPd->DeepCopy(grouper->GetOutput());
  }
  else
  {
    vtkNew(vtkSVSurfaceCenterlineGrouper, grouper);
    grouper->SetInputData(this->WorkPd);
    grouper->SetPolycubePd(this->PolycubePd);
    grouper->SetMergedCenterlines(this->MergedCenterlines);
    grouper->SetUseRadiusInformation(this->UseRadiusInformation);
    grouper->SetCenterlineRadiusArrayName(this->CenterlineRadiusArrayName);
    grouper->SetCenterlineGroupIdsArrayName(this->CenterlineGroupIdsArrayName);
    grouper->SetCenterlineIdsArrayName("CenterlineIds");
    grouper->SetGroupIdsArrayName(this->GroupIdsArrayName);
    grouper->SetTractIdsArrayName("TractIds");
    grouper->GroupSurfaceOn();
    grouper->EnforceCenterlinesConnectivityOn();
    grouper->EnforcePolycubeConnectivityOn();
    grouper->DebugOn();
    grouper->Update();

    this->WorkPd->DeepCopy(grouper->GetOutput());
  }

  vtkNew(vtkSVSurfaceCenterlineAttributesPasser, passer);
  passer->SetInputData(this->WorkPd);
  passer->SetPolycubePd(this->PolycubePd);
  passer->SetMergedCenterlines(this->MergedCenterlines);
  passer->SetUseRadiusInformation(this->UseRadiusInformation);
  passer->SetCenterlineRadiusArrayName(this->CenterlineRadiusArrayName);
  passer->SetCenterlineGroupIdsArrayName(this->CenterlineGroupIdsArrayName);
  passer->SetCenterlineIdsArrayName("CenterlineIds");
  passer->SetGroupIdsArrayName(this->GroupIdsArrayName);
  passer->SetTractIdsArrayName("TractIds");
  passer->SetNormalsWeighting(this->NormalsWeighting);
  passer->SetIsVasculature(this->IsVasculature);
  passer->SetBoundaryEnforceFactor(this->BoundaryEnforceFactor);
  passer->SetEnforceBoundaryDirections(this->EnforceBoundaryDirections);
  passer->Update();

  this->WorkPd->DeepCopy(passer->GetOutput());

  vtkNew(vtkSVSurfaceCuboidPatcher, patcher);
  patcher->SetInputData(this->WorkPd);
  patcher->SetPolycubePd(this->PolycubePd);
  patcher->SetMergedCenterlines(this->MergedCenterlines);
  patcher->SetCenterlineRadiusArrayName(this->CenterlineRadiusArrayName);
  patcher->SetCenterlineGroupIdsArrayName(this->CenterlineGroupIdsArrayName);
  patcher->SetCenterlineIdsArrayName("CenterlineIds");
  patcher->SetGroupIdsArrayName(this->GroupIdsArrayName);
  patcher->SetTractIdsArrayName("TractIds");
  patcher->Update();

  this->WorkPd->DeepCopy(patcher->GetOutput());

  vtkNew(vtkSVParameterizeSurfaceOnPolycube, surfParameterizer);
  surfParameterizer->SetInputData(this->WorkPd);
  surfParameterizer->SetPolycubePd(this->PolycubePd);
  surfParameterizer->SetGroupIdsArrayName(this->GroupIdsArrayName);
  surfParameterizer->Update();

  vtkNew(vtkSVParameterizeVolumeOnPolycube, volParameterizer);
  volParameterizer->SetInputData(this->WorkPd);
  volParameterizer->SetPolycubeUg(this->PolycubeUg);
  volParameterizer->SetSurfaceOnPolycubePd(surfParameterizer->GetOutput());
  volParameterizer->SetGroupIdsArrayName(this->GroupIdsArrayName);
  volParameterizer->Update();

  this->FinalHexMesh->DeepCopy(volParameterizer->GetFinalHexMesh());

  return SV_OK;
}

// ----------------------
// MergeCenterlines
// ----------------------
int vtkSVGroupsSegmenter::MergeCenterlines()
{
  if (vtkSVGeneralUtils::CheckArrayExists(this->Centerlines, 1, this->GroupIdsArrayName) != SV_OK)
  {
    std::cout<<"Splitting centerlines..."<<endl;
    vtkNew(vtkSVCenterlineBranchSplitter, branchSplitter);
    branchSplitter->SetInputData(this->Centerlines);
    branchSplitter->SetGroupingModeToFirstPoint();
    branchSplitter->SetBlankingArrayName("Blanking");
    branchSplitter->SetRadiusArrayName(this->CenterlineRadiusArrayName);
    branchSplitter->SetGroupIdsArrayName("GroupIds");
    branchSplitter->SetCenterlineIdsArrayName("CenterlineIds");
    branchSplitter->SetTractIdsArrayName("TractIds");
    branchSplitter->SetRadiusMergeRatio(this->RadiusMergeRatio);
    branchSplitter->SetUseAbsoluteMergeDistance(this->UseAbsoluteMergeDistance);
    branchSplitter->SetMergeDistance(this->MergeDistance);
    branchSplitter->Update();

    this->Centerlines->DeepCopy(branchSplitter->GetOutput());
  }

  fprintf(stdout,"Merging centerlines...\n");
  vtkNew(vtkvmtkMergeCenterlines, merger);
  merger->SetInputData(this->Centerlines);
  merger->SetRadiusArrayName(this->CenterlineRadiusArrayName);
  merger->SetGroupIdsArrayName(this->GroupIdsArrayName);
  merger->SetCenterlineIdsArrayName("CenterlineIds");
  merger->SetTractIdsArrayName("TractIds");
  merger->SetBlankingArrayName(this->BlankingArrayName);
  merger->SetResamplingStepLength(0.0);
  merger->SetMergeBlanked(1);
  merger->Update();

  int numFullPts = merger->GetOutput()->GetNumberOfPoints();

  vtkNew(vtkCleanPolyData, lineCleaner);
  lineCleaner->SetInputData(merger->GetOutput());
  lineCleaner->Update();

  this->MergedCenterlines->DeepCopy(lineCleaner->GetOutput());
  this->MergedCenterlines->BuildLinks();

  if (!this->IsVasculature)
  {
    int numRemove = this->NumberOfCenterlineRemovePts;
    vtkNew(vtkPoints, newPoints);
    vtkNew(vtkPointData, newPointData);
    newPointData->CopyAllocate(this->MergedCenterlines->GetPointData(),
                               numFullPts);

    vtkNew(vtkCellArray, newCells);
    vtkNew(vtkCellData, newCellData);
    newCellData->CopyAllocate(this->MergedCenterlines->GetCellData());

    for (int i=0; i<this->MergedCenterlines->GetNumberOfCells(); i++)
    {
      vtkIdType npts, *pts;
      this->MergedCenterlines->GetCellPoints(i, npts, pts);

      vtkNew(vtkIdList, point0CellIds);
      this->MergedCenterlines->GetPointCells(pts[0], point0CellIds);

      vtkNew(vtkIdList, pointNCellIds);
      this->MergedCenterlines->GetPointCells(pts[npts-1], pointNCellIds);

      vtkNew(vtkPolyLine, newLine);
      if (point0CellIds->GetNumberOfIds() > 1)
      {
        for (int j=0; j<numRemove; j++)
        {
          int newPointId = newPoints->InsertNextPoint(
            this->MergedCenterlines->GetPoint(pts[j]));

          newLine->GetPointIds()->InsertNextId(newPointId);

          newPointData->CopyData(this->MergedCenterlines->GetPointData(),
            pts[j], newPointId);
        }
      }

      for (int j=numRemove; j<npts-numRemove; j++)
      {
        int newPointId = newPoints->InsertNextPoint(
          this->MergedCenterlines->GetPoint(pts[j]));
        newLine->GetPointIds()->InsertNextId(newPointId);

        newPointData->CopyData(this->MergedCenterlines->GetPointData(),
          pts[j], newPointId);
      }

      if (pointNCellIds->GetNumberOfIds() > 1)
      {
        for (int j=numRemove; j>0; j--)
        {
          int newPointId = newPoints->InsertNextPoint(
            this->MergedCenterlines->GetPoint(pts[npts-j]));
          newLine->GetPointIds()->InsertNextId(newPointId);

          newPointData->CopyData(this->MergedCenterlines->GetPointData(),
            pts[npts-j], newPointId);
        }
      }

      newCells->InsertNextCell(newLine);
      newCellData->CopyData(this->MergedCenterlines->GetCellData(), i, i);
    }

    this->MergedCenterlines->Reset();
    this->MergedCenterlines->SetPoints(newPoints);
    this->MergedCenterlines->SetLines(newCells);

    newPointData->Squeeze();
    this->MergedCenterlines->GetPointData()->PassData(newPointData);
    this->MergedCenterlines->GetCellData()->PassData(newCellData);

    vtkNew(vtkCleanPolyData, cleaner);
    cleaner->SetInputData(this->MergedCenterlines);
    cleaner->Update();

    this->MergedCenterlines->DeepCopy(cleaner->GetOutput());
    this->MergedCenterlines->BuildLinks();
  }

  fprintf(stdout,"Merged\n");

  return SV_OK;
}

// ----------------------
// PrintSelf
// ----------------------
void vtkSVGroupsSegmenter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);
  os << indent << "Clip value: " << this->ClipValue << "\n";
  os << indent << "Cutoff Radius Factor: " << this->CutoffRadiusFactor << "\n";
  os << indent << "Use radius information: " << this->UseRadiusInformation << "\n";
  if (this->CenterlineGroupIdsArrayName != NULL)
    os << indent << "Centerline group ids name: " << this->CenterlineGroupIdsArrayName << "\n";
  if (this->CenterlineRadiusArrayName != NULL)
    os << indent << "Centerline radius array name: " << this->CenterlineRadiusArrayName << "\n";
  if (this->GroupIdsArrayName != NULL)
    os << indent << "Group ids array name: " << this->GroupIdsArrayName << "\n";
  if (this->BlankingArrayName != NULL)
    os << indent << "Blanking array name: " << this->BlankingArrayName << "\n";
}
