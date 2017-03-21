/*=========================================================================
 *
 * Copyright (c) 2015 The Regents of the University of California.
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

/** @file vtkSVConstrainedBlend.cxx
 *  @brief This implements the vtkSVConstrainedBlend filter as a class
 *
 *  @author Adam Updegrove
 *  @author updega2@gmail.com
 *  @author UC Berkeley
 *  @author shaddenlab.berkeley.edu
 */

#include "vtkSVConstrainedBlend.h"

#include "vtkFloatArray.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPolyData.h"
#include "vtkUnstructuredGrid.h"
#include "vtkCellArray.h"
#include "vtkIntArray.h"
#include "vtkDoubleArray.h"
#include "vtkCellData.h"
#include "vtkPointData.h"
#include "vtkSmartPointer.h"
#include "vtkSVConstrainedSmoothing.h"
#include "vtkSVGeneralUtils.h"
#include "vtkSVGlobals.h"
#include "vtkSVLocalLoopSubdivisionFilter.h"
#include "vtkSVLocalSmoothPolyDataFilter.h"
#include "vtkSVLocalQuadricDecimation.h"
#include "vtkPolyDataNormals.h"
#include "vtkCellLocator.h"
#include "vtkGenericCell.h"
#include "vtkMath.h"

#include <iostream>

vtkStandardNewMacro(vtkSVConstrainedBlend);

vtkSVConstrainedBlend::vtkSVConstrainedBlend()
{
    this->CellArrayName = 0;
    this->PointArrayName = 0;
    this->Weight = 0.2;

    this->UsePointArray = 0;
    this->UseCellArray = 0;

    this->NumBlendOperations = 2;
    this->NumSubBlendOperations = 2;
    this->NumConstrainedSmoothOperations = 3;
    this->NumLapSmoothOperations = 50;
    this->RelaxationFactor = 0.01;
    this->NumGradientSolves = 20;
    this->DecimationTargetReduction = 0.01;
    this->NumSubdivisionIterations = 1;
}

vtkSVConstrainedBlend::~vtkSVConstrainedBlend()
{
}

void vtkSVConstrainedBlend::PrintSelf(ostream& os, vtkIndent indent)
{
}

// Generate Separated Surfaces with Region ID Numbers
int vtkSVConstrainedBlend::RequestData(
                                 vtkInformation *vtkNotUsed(request),
                                 vtkInformationVector **inputVector,
                                 vtkInformationVector *outputVector)
{
    // get the input and output
    vtkPolyData *input = vtkPolyData::GetData(inputVector[0]);
    vtkPolyData *output = vtkPolyData::GetData(outputVector);

    // Define variables used by the algorithm
    vtkNew(vtkPoints, inpts);
    vtkNew(vtkCellArray, inPolys);
    vtkIdType numPts, numPolys;
    vtkIdType newId, cellId,pointId;

    //Get input points, polys and set the up in the vtkPolyData mesh
    inpts = input->GetPoints();
    inPolys = input->GetPolys();

    //Get the number of Polys for scalar  allocation
    numPolys = input->GetNumberOfPolys();
    numPts = input->GetNumberOfPoints();

    //Check the input to make sure it is there
    if (numPolys < 1)
    {
        vtkDebugMacro("No input!");
	return 1;
    }

    if (this->UsePointArray)
    {
      if (this->GetArrays(input,0) != 1)
      {
	std::cout<<"No Point Array Named "<<this->PointArrayName<<" on surface"<<endl;
	return 0;
      }
    }
    if (this->UseCellArray)
    {
      if (this->GetArrays(input,1) != 1)
      {
	std::cout<<"No Cell Array Named "<<this->CellArrayName<<" on surface"<<endl;
	return 0;
      }
    }

    vtkNew(vtkPolyData, tmp);
    tmp->DeepCopy(input);
    for (int i=0;i<this->NumBlendOperations;i++)
    {
      for (int j=0;j<this->NumSubBlendOperations;j++)
      {
        std::cout<<"ConstrainedSmooth!"<<endl;
	this->ConstrainedSmooth(tmp);

        std::cout<<"LapSmooth!"<<endl;
	this->LaplacianSmooth(tmp);

        std::cout<<"Decimate!"<<endl;
        this->Decimate(tmp);
      }
      std::cout<<"Subdivide!"<<endl;
      this->Subdivide(tmp);
    }

    output->DeepCopy(tmp);
    return 1;
}

int vtkSVConstrainedBlend::GetArrays(vtkPolyData *object,int type)
{
  vtkIdType i;
  int exists = 0;
  int numArrays;

  if (type == 0)
  {
    numArrays = object->GetPointData()->GetNumberOfArrays();
    for (i=0;i<numArrays;i++)
    {
      if (!strcmp(object->GetPointData()->GetArrayName(i),
	    this->PointArrayName))
      {
	exists = 1;
      }
    }
  }
  else
  {
    numArrays = object->GetCellData()->GetNumberOfArrays();
    for (i=0;i<numArrays;i++)
    {
      if (!strcmp(object->GetCellData()->GetArrayName(i),
	    this->CellArrayName))
      {
	exists = 1;
      }
    }
  }

  if (exists)
  {
    if (type == 0)
    {
      this->PointArray = vtkIntArray::SafeDownCast(
	  object->GetPointData()->GetArray(this->PointArrayName));
    }
    else
    {
      this->CellArray = vtkIntArray::SafeDownCast(
	  object->GetCellData()->GetArray(this->CellArrayName));
    }

  }

  return exists;
}

int vtkSVConstrainedBlend::Decimate(vtkPolyData *pd) {
  vtkNew(vtkSVLocalQuadricDecimation, decimator);
  decimator->SetInputData(pd);
  if (this->UsePointArray)
  {
    decimator->SetDecimatePointArrayName(this->PointArrayName);
    decimator->UsePointArrayOn();
  }
  if (this->UseCellArray)
  {
    decimator->SetDecimateCellArrayName(this->CellArrayName);
    decimator->UseCellArrayOn();
  }
  decimator->SetTargetReduction(this->DecimationTargetReduction);
  decimator->Update();
  pd->DeepCopy(decimator->GetOutput());

  if (this->UsePointArray)
  {
    if (this->GetArrays(pd,0) != 1)
    {
      std::cout<<"No Point Array Named "<<this->PointArrayName<<" on surface"<<endl;
      return 0;
    }
  }
  if (this->UseCellArray)
  {
    if (this->GetArrays(pd,1) != 1)
    {
      std::cout<<"No Point Array Named "<<this->CellArrayName<<" on surface"<<endl;
      return 0;
    }
  }

  return 1;
}

int vtkSVConstrainedBlend::Subdivide(vtkPolyData *pd) {
  vtkNew(vtkSVLocalLoopSubdivisionFilter, subdivider);

  subdivider->SetInputData(pd);
  subdivider->SetNumberOfSubdivisions(this->NumSubdivisionIterations);
  if (this->UseCellArray)
  {
    subdivider->SetSubdivideCellArrayName(this->CellArrayName);
    subdivider->UseCellArrayOn();
  }
  if (this->UsePointArray)
  {
    subdivider->SetSubdividePointArrayName(this->PointArrayName);
    subdivider->UsePointArrayOn();
  }
  subdivider->Update();
  pd->DeepCopy(subdivider->GetOutput());

  if (this->UsePointArray)
  {
    if (this->GetArrays(pd,0) != 1)
    {
      std::cout<<"No Point Array Named "<<this->PointArrayName<<" on surface"<<endl;
      return 0;
    }
  }
  if (this->UseCellArray)
  {
    if (this->GetArrays(pd,1) != 1)
    {
      std::cout<<"No Point Array Named "<<this->CellArrayName<<" on surface"<<endl;
      return 0;
    }
  }
  return 1;
}

int vtkSVConstrainedBlend::ConstrainedSmooth(vtkPolyData *pd)
{
  vtkNew(vtkSVConstrainedSmoothing, smoother);
  smoother->SetInputData(pd);
  smoother->SetNumGradientSolves(this->NumGradientSolves);
  smoother->SetNumSmoothOperations(this->NumConstrainedSmoothOperations);
  if (this->UsePointArray)
  {
    smoother->SetPointArrayName(this->PointArrayName);
    smoother->UsePointArrayOn();
  }
  if (this->UseCellArray)
  {
    smoother->SetCellArrayName(this->CellArrayName);
    smoother->UseCellArrayOn();
  }
  smoother->SetWeight(this->Weight);
  smoother->Update();

  pd->DeepCopy(smoother->GetOutput());
  return 1;
}

int vtkSVConstrainedBlend::LaplacianSmooth(vtkPolyData *pd)
{
  vtkNew(vtkSVLocalSmoothPolyDataFilter, smoother);
  smoother->SetInputData(pd);
  if (this->UsePointArray)
  {
    smoother->SetSmoothPointArrayName(this->PointArrayName);
    smoother->UsePointArrayOn();
  }
  if (this->UseCellArray)
  {
    smoother->SetSmoothCellArrayName(this->CellArrayName);
    smoother->UseCellArrayOn();
  }
  smoother->SetNumberOfIterations(this->NumLapSmoothOperations);
  smoother->SetRelaxationFactor(this->RelaxationFactor);
  smoother->BoundarySmoothingOff();
  smoother->Update();

  pd->DeepCopy(smoother->GetOutput());
  return 1;
}


