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

#include "vtkSVNURBSCurve.h"

#include "vtkCleanPolyData.h"
#include "vtkSVNURBSUtils.h"
#include "vtkPointData.h"
#include "vtkObjectFactory.h"
#include "vtkSmartPointer.h"
#include "vtkSparseArray.h"
#include "vtkSVGlobals.h"

// ----------------------
// StandardNewMacro
// ----------------------
vtkStandardNewMacro(vtkSVNURBSCurve);

// ----------------------
// Constructor
// ----------------------
vtkSVNURBSCurve::vtkSVNURBSCurve()
{
  this->NumberOfControlPoints = 0;
  this->NumberOfKnotPoints    = 0;
  this->Degree                = 0;
  this->Clamped               = 1;
  this->Closed                = 0;

  this->ControlPointGrid   = vtkSVControlGrid::New();
  this->KnotVector         = vtkDoubleArray::New();

  this->CurveRepresentation = vtkPolyData::New();
}

// ----------------------
// Destructor
// ----------------------
vtkSVNURBSCurve::~vtkSVNURBSCurve()
{
  if (this->ControlPointGrid != NULL)
  {
    this->ControlPointGrid->Delete();
  }
  if (this->KnotVector != NULL)
  {
    this->KnotVector->Delete();
  }

  if (this->CurveRepresentation != NULL)
  {
    this->CurveRepresentation->Delete();
  }
}

// ----------------------
// DeepCopy
// ----------------------
void vtkSVNURBSCurve::DeepCopy(vtkSVNURBSCurve *src)
{
  this->Superclass::DeepCopy(src);

  this->SetNumberOfControlPoints(src->GetNumberOfControlPoints());
  this->SetNumberOfKnotPoints(src->GetNumberOfKnotPoints());
  this->SetDegree(src->GetDegree());

  this->ControlPointGrid->DeepCopy(src->GetControlPointGrid());
  this->KnotVector->DeepCopy(src->GetKnotVector());

  this->CurveRepresentation->DeepCopy(src->GetCurveRepresentation());
}


// ----------------------
// PrintSelf
// ----------------------
void vtkSVNURBSCurve::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "Number of control points: " << this->NumberOfControlPoints << "\n";
  os << indent << "Number of knot points: " << this->NumberOfKnotPoints << "\n";
  os << indent << "Degree: " << this->Degree << "\n";
  os << indent << "Clamped: " << this->Clamped << "\n";
  os << indent << "Closed: " << this->Closed << "\n";
}

// ----------------------
// Initialize
// ----------------------
void vtkSVNURBSCurve::Initialize()
{
  this->Superclass::Initialize();
}

// ----------------------
// GetData
// ----------------------
vtkSVNURBSCurve* vtkSVNURBSCurve::GetData(vtkInformation* info)
{
  return info? vtkSVNURBSCurve::SafeDownCast(info->Get(DATA_OBJECT())) : 0;
}

// ----------------------
// GetData
// ----------------------
vtkSVNURBSCurve* vtkSVNURBSCurve::GetData(vtkInformationVector* v, int i)
{
  return vtkSVNURBSCurve::GetData(v->GetInformationObject(i));
}

// ----------------------
// SetControlPoints
// ----------------------
void vtkSVNURBSCurve::SetControlPoints(vtkPoints *points1d)
{
  // Get number of points
  int nCon = points1d->GetNumberOfPoints();

  // Set the dimensions of control point grid
  this->ControlPointGrid->SetDimensions(nCon, 1, 1);

  // Set the points
  this->ControlPointGrid->SetPoints(points1d);

  // Loop through points and set weight
  vtkNew(vtkDoubleArray, weights);
  weights->SetNumberOfTuples(nCon);
  for (int i=0; i<nCon; i++)
    weights->SetTuple1(i, 1.0);
  weights->SetName("Weights");

  // Set the array on the grid
  this->ControlPointGrid->GetPointData()->AddArray(weights);

  // Update number of control points
  this->NumberOfControlPoints = nCon;
}

// ----------------------
// SetControlPoints
// ----------------------
int vtkSVNURBSCurve::InsertKnot(const double newKnot, const int numberOfInserts)
{
  // Get current info about curve degree
  int p = this->Degree;

  // Get the location of the knot
  int span=0;
  vtkSVNURBSUtils::FindSpan(p, newKnot, this->KnotVector, span);

  // If the value at the location is our value, we need to get current mult
  int mult=0;
  if(this->KnotVector->GetTuple1(span) == newKnot)
  {
    int i=span;
    int count = 1;
    while(this->KnotVector->GetTuple1(i+1) == this->KnotVector->GetTuple1(i) &&
          i < this->KnotVector->GetNumberOfTuples())
    {
      count++;
      i++;
    }
    mult = count;
  }

  // Set up new knots and points
  vtkNew(vtkDoubleArray, newKnots);
  vtkNew(vtkSVControlGrid, newControlPoints);

  // Insert the knot
  if (vtkSVNURBSUtils::CurveInsertKnot(this->ControlPointGrid,
                                       this->KnotVector,
                                       p, newKnot, span,
                                       mult, numberOfInserts,
                                       newControlPoints, newKnots) != SV_OK)
  {
    vtkErrorMacro("Error on knot insertion");
    return SV_ERROR;
  }

  // Replace existing data with new data
  this->SetControlPointGrid(newControlPoints);
  this->SetKnotVector(newKnots);

  return SV_OK;

}

// ----------------------
// SetKnotVector
// ----------------------
int vtkSVNURBSCurve::SetKnotVector(vtkDoubleArray *knots)
{
  this->KnotVector->DeepCopy(knots);

  this->NumberOfKnotPoints = this->KnotVector->GetNumberOfTuples();
  return SV_OK;
}

// ----------------------
// SetControlPointGrid
// ----------------------
int vtkSVNURBSCurve::SetControlPointGrid(vtkSVControlGrid *controlPoints)
{
  this->ControlPointGrid->DeepCopy(controlPoints);

  int dims[3];
  this->ControlPointGrid->GetDimensions(dims);
  this->NumberOfControlPoints = dims[0];
  return SV_OK;
}

// ----------------------
// GetControlPoint
// ----------------------
int vtkSVNURBSCurve::GetControlPoint(const int index, double coordinates[3], double &weight)
{
  this->ControlPointGrid->GetControlPoint(index, 0, 0, coordinates, weight);
  return SV_OK;
}

// ----------------------
// GetControlPoint
// ----------------------
int vtkSVNURBSCurve::GetControlPoint(const int index, double pw[4])
{
  this->ControlPointGrid->GetControlPoint(index, 0, 0, pw);
  return SV_OK;
}

// ----------------------
// GeneratePolyDataRepresentation
// ----------------------
int vtkSVNURBSCurve::GeneratePolyDataRepresentation(const double spacing)
{
  // Get number of control points and knots
  this->NumberOfControlPoints = this->ControlPointGrid->GetNumberOfPoints();
  this->NumberOfKnotPoints = this->KnotVector->GetNumberOfTuples();
  int nCon  = this->NumberOfControlPoints;
  int nKnot = this->NumberOfKnotPoints;
  if (nCon == 0)
  {
    vtkErrorMacro("No control points");
    return SV_ERROR;
  }
  if (nKnot == 0)
  {
    vtkErrorMacro("No knot points");
    return SV_ERROR;
  }

  // Using clamped formula for degree of curve
  int p = nKnot - nCon - 1;

  // Get number of divisions from spacing
  int numDiv = ceil(1.0/spacing);

  // Get the parameter sampling
  vtkNew(vtkDoubleArray, uEvals);
  vtkSVNURBSUtils::LinSpace(0, 1, numDiv, uEvals);

  //If nCon - 1 < p, not possible with clamping
  //If nCon - 1 = p, bezier with clamping
  //If nCon - 1 > p, fantastic

  // Get sparse array for Nu
  vtkNew(vtkSparseArray<double>, Nus);
  Nus->Resize(numDiv, p+2);

  // Get sparse array for basis functions
  vtkNew(vtkSparseArray<double>, Nfinal);
  Nfinal->Resize(numDiv, nCon);

  // Get double array for rational basis functions
  vtkNew(vtkDoubleArray, Nrational);
  Nrational->SetNumberOfTuples(numDiv);
  Nrational->FillComponent(0, 0.0);

  // Loop through control points
  for (int i=0; i<nCon; i++)
  {
    // Evaluate the basis functions
    if (vtkSVNURBSUtils::BasisEvaluationVec(this->KnotVector, p,
                                       i, uEvals, Nus) != SV_OK)
    {
      return SV_ERROR;
    }
    // for each sampling get the final basis functions
    for (int j=0; j<numDiv; j++)
    {
      Nfinal->SetValue(j, i, Nus->GetValue(j, 0));

    }
  }

  vtkDataArray *weights = this->ControlPointGrid->GetPointData()->GetArray("Weights");
  // Last value should be 1
  Nfinal->SetValue(numDiv-1, nCon-1, 1.0);

  // Multiply by weights for rational curve
  for (int i=0; i<numDiv; i++)
  {
    double ratVal = 0.0;
    for (int j=0; j<nCon; j++)
    {
      double val       = Nfinal->GetValue(i, j);
      double ratWeight = weights->GetTuple1(j);
      ratVal = ratVal + val*ratWeight;
    }
    Nrational->SetTuple1(i, ratVal);
  }
  // Now that we have denominator, calculate numerators!
  for (int i=0; i<numDiv; i++)
  {
    for (int j=0; j<nCon; j++)
    {
      double val       = Nfinal->GetValue(i, j);
      double ratVal    = Nrational->GetTuple1(i);
      double ratWeight = weights->GetTuple1(j);
      Nfinal->SetValue(i, j, val*ratWeight/ratVal);
    }
  }

  // Must multiply the basis functions by our control grid to get the final
  // point set!
  vtkNew(vtkPoints, surfacePoints);
  if(vtkSVNURBSUtils::MatrixPointsMultiply(Nfinal, this->ControlPointGrid->GetPoints(),
                                       surfacePoints) != SV_OK)
  {
    return SV_ERROR;
  }

  // Get connectivity of our point set
  vtkNew(vtkCellArray, surfaceLines);
  this->GetStructuredGridConnectivity(numDiv, surfaceLines);

  // Update the curve representation
  this->CurveRepresentation->SetPoints(surfacePoints);
  this->CurveRepresentation->SetLines(surfaceLines);

  // Clean the curve in case of duplicate points (closed curve)
  vtkNew(vtkCleanPolyData, cleaner);
  cleaner->SetInputData(this->CurveRepresentation);
  cleaner->Update();

  // Get clean output and build links
  this->CurveRepresentation->DeepCopy(cleaner->GetOutput());
  this->CurveRepresentation->BuildLinks();

  return SV_OK;
}

// ----------------------
// GetStructuredGridConnectivity
// ----------------------
int vtkSVNURBSCurve::GetStructuredGridConnectivity(const int numPoints, vtkCellArray *connectivity)
{
  connectivity->Reset();
  vtkNew(vtkIdList, ptIds);
  ptIds->SetNumberOfIds(2);
  for (int i=0; i< numPoints - 1; i++)
  {
    ptIds->SetId(0, i);
    ptIds->SetId(1, i+1);
    connectivity->InsertNextCell(ptIds);
  }

  return SV_OK;
}

// ----------------------
// GetMultiplicity
// ----------------------
int vtkSVNURBSCurve::GetMultiplicity(vtkIntArray *multiplicity, vtkDoubleArray *singleKnots)
{
  vtkSVNURBSUtils::GetMultiplicity(this->KnotVector, multiplicity, singleKnots);
  return SV_OK;
}
