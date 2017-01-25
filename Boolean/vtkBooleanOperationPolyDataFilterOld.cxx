/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkBooleanOperationPolyDataFilterOld.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
/** @file vtkBooleanOperationPolyDataFilterOld.cxx
 *  @brief This is the filter to perform boolean operations
 *  @author Adam Updegrove
 *  @author updega2@gmail.com
 */

#include "vtkBooleanOperationPolyDataFilterOld.h"

#include "vtkCellData.h"
#include "vtkDataSetSurfaceFilter.h"
#include "vtkDoubleArray.h"
#include "vtkFloatArray.h"
#include "vtkGenericCell.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkIntersectionPolyDataFilterOld.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkSmartPointer.h"
#include "vtkThreshold.h"
#include "vtkPolyDataNormals.h"
#include "vtkTransform.h"
#include "vtkTransformPolyDataFilter.h"
#include "vtkCellArray.h"
#include "vtkTriangle.h"
#include "vtkMath.h"
#include "vtkMergeCells.h"
#include "vtkAppendPolyData.h"
#include "vtkUnstructuredGrid.h"
#include "vtkCleanPolyData.h"
#include "vtkPointLocator.h"
#include "vtkXMLPolyDataWriter.h"

#include <string>
#include <sstream>
#include <iostream>
#include <list>

//Function to get the directory from the input File Name
//For example, /User/Adam.stl returns /User
std::string getP(std::string fullName)
{
  std::string pathName;
  unsigned split = fullName.find_last_of("/\\");
  pathName = fullName.substr(0,split);
  return pathName;
}

std::string getRaw(std::string fullName)
{
  std::string rawName;
  unsigned split = fullName.find_last_of("/\\");
  rawName = fullName.substr(split+1);
  rawName.erase(rawName.find_last_of("."),std::string::npos);
  return rawName;
}
void WriteVTP(std::string inputFilename,vtkPolyData *writePolyData,std::string attachName)
{
  std::string rawName, pathName, outputFilename;

  vtkSmartPointer<vtkXMLPolyDataWriter> writer  = vtkSmartPointer<vtkXMLPolyDataWriter>::New();

  pathName = getP(inputFilename);
  rawName = getRaw(inputFilename);

  outputFilename = pathName+"/"+rawName+attachName+".vtp";

  writer->SetFileName(outputFilename.c_str());
#if VTK_MAJOR_VERSION <= 5
  writer->SetInput(writePolyData);
#else
  writer->SetInputData(writePolyData);
#endif
  //writer->SetDataModeToAscii();

  writer->Write();
}

//----------------------------------------------------------------------------
// Helper typedefs and data structure.
struct simLine
{
  vtkIdType id;
  vtkIdType pt1;
  vtkIdType pt2;
};
struct simLoop
{
  std::list<simLine> cells;
  vtkIdType startPt;
  vtkIdType endPt;
  int loopType;
};

//Implementation function
class vtkBooleanOperationPolyDataFilterOld::Impl
{
public:
  Impl();
  virtual ~Impl();

  void Initialize();
  void SetCheckArrays();
  void SetBoundaryArrays();
  void ResetCheckArrays();
  void GetBooleanRegions(int inputIndex, std::vector<simLoop> *loops);
  void DetermineIntersection(std::vector<simLoop> *loops);
  void PerformBoolean(vtkPolyData *output,int Operation);
  void ThresholdRegions(vtkPolyData **surfaces);

protected:

  int RunLoopFind(vtkIdType interPt,vtkIdType nextCell,
      bool *usedPt, simLoop *loop);

  int RunLoopTest(vtkIdType interPt,vtkIdType nextCell, simLoop *loop,
      bool *usedPt);

  int GetCellOrientation(vtkPolyData *pd,vtkIdType cellId, vtkIdType p0,
      vtkIdType p1, int index);

  int FindRegion(int inputIndex, int fillnumber, int start,int fill);

  int FindRegionTipToe(int inputIndex, int fillnumber,int fill);

public:

  int intersectionCase;
  int Verbose;

  vtkPolyData *Mesh[2];
  vtkPolyData *IntersectionLines;

  vtkIntArray *BoundaryPointArray[2];
  vtkIntArray *BoundaryCellArray[2];
  vtkIntArray *BooleanArray[2];
  vtkIntArray *NewCellIds[2];

  vtkIdType *checked[2];
  vtkIdType *checkedcarefully[2];
  vtkIdType *pointMapper[2];
  vtkIdType *reversePointMapper[2];

  vtkIdList *CheckCells;
  vtkIdList *CheckCells2;
  vtkIdList *CheckCellsCareful;
  vtkIdList *CheckCellsCareful2;
};

vtkBooleanOperationPolyDataFilterOld::Impl::Impl() :
  CheckCells(0), CheckCells2(0), CheckCellsCareful(0),
  CheckCellsCareful2(0)
{
  for (int i = 0;i<2;i++)
  {
    this->Mesh[i] = vtkPolyData::New();

    this->BooleanArray[i] = vtkIntArray::New();
    this->BoundaryPointArray[i] = vtkIntArray::New();
    this->BoundaryCellArray[i] = vtkIntArray::New();
    this->NewCellIds[i] = vtkIntArray::New();

    this->checked[i] = NULL;
    this->checkedcarefully[i] = NULL;
    this->pointMapper[i] = NULL;
    this->reversePointMapper[i] = NULL;
  }
  this->IntersectionLines = vtkPolyData::New();
  this->CheckCells = vtkIdList::New();
  this->CheckCells2 = vtkIdList::New();
  this->CheckCellsCareful = vtkIdList::New();
  this->CheckCellsCareful2 = vtkIdList::New();

  //Intersection Case:
  //0 -> Only hard closed intersection loops
  //1 -> At least one soft closed intersection loop
  //2 -> At least one open intersection loop
  this->intersectionCase = 0;
  this->Verbose = 0;
}

vtkBooleanOperationPolyDataFilterOld::Impl::~Impl()
{
  for (int i = 0;i<2;i++)
  {
    this->Mesh[i]->Delete();
    this->BooleanArray[i]->Delete();
    this->BoundaryPointArray[i]->Delete();
    this->BoundaryCellArray[i]->Delete();
    this->NewCellIds[i]->Delete();

    //if (this->checked[i] != NULL)
      delete [] this->checked[i];
    //if (this->checkedcarefully[i] != NULL)
      delete [] this->checkedcarefully[i];
    //if (this->pointMapper[i] != NULL)
      delete [] this->pointMapper[i];
    //if (this->reversePointMapper[i] != NULL)
      delete [] this->reversePointMapper[i];
  }
  this->IntersectionLines->Delete();
  this->CheckCells->Delete();
  this->CheckCells2->Delete();
  this->CheckCellsCareful->Delete();
  this->CheckCellsCareful2->Delete();
}

//Flood fill algorithm to find region of mesh separated by intersection lines
int vtkBooleanOperationPolyDataFilterOld::Impl::FindRegion(int inputIndex,
    int fillnumber, int start,int fill)
{
    if (this->Verbose)
    {
      std::cout<<"Finding region with fill "<<fillnumber<<" of mesh "
        <<inputIndex<<" with cellID "<<this->CheckCells->GetId(0)<<endl;
    }
    //Variables used in function
    int i;
    double boundarypt[3];
    double pt1[3];
    double pt2[3];
    vtkIdType j,k,l;
    vtkIdType *pts = 0;
    vtkIdType npts = 0;
    vtkIdType cellId;
    vtkIdType numNei, nei, p1, p2, nIds, neiId;

    //Id List to store neighbor cells for each set of nodes and a cell
    vtkSmartPointer<vtkIdList> neighbors = vtkSmartPointer<vtkIdList>::New();
    vtkSmartPointer<vtkIdList> tmp = vtkSmartPointer<vtkIdList>::New();

    vtkIdType numCheckCells;
    //Get neighboring cell for each pair of points in current cell
    //While there are still cells to be checked, find neighbor cells
    while ((numCheckCells = this->CheckCells->GetNumberOfIds()) > 0)
    {
      for (int c=0;c<numCheckCells;c++)
      {
        cellId = this->CheckCells->GetId(c);
        //Get the three points of the cell
        this->Mesh[inputIndex]->GetCellPoints(cellId,npts,pts);
        if (this->checked[inputIndex][cellId] == 0)
        {
          //Mark cell as checked and insert the fillnumber value to cell
          if (fill)
            this->BooleanArray[inputIndex]->InsertValue(cellId,fillnumber);
          this->checked[inputIndex][cellId] = 1;
          for (i=0; i < npts; i++)
          {
            p1 = pts[i];
            //Get the cells attached to each point
            this->Mesh[inputIndex]->GetPointCells(p1,neighbors);
            numNei = neighbors->GetNumberOfIds();

            //For each neighboring cell
            for (j=0;j < numNei;j++)
            {
              //If this cell is close to a boundary
              if (this->BoundaryCellArray[inputIndex]->
                  GetValue(neighbors->GetId(j)))
              {
                //If this cell hasn't been checked already
                if (this->checkedcarefully[inputIndex][neighbors->
                    GetId(j)] == 0)
                {
                  //Add this cell to the careful check cells list and run
                  //the region finding tip toe code
                  this->CheckCellsCareful->
                    InsertNextId(neighbors->GetId(j));
                  if (fill)
                  {
                    this->FindRegionTipToe(inputIndex,fillnumber,1);
                  }
                  else
                  {
                    this->FindRegionTipToe(inputIndex,fillnumber,0);
                  }
                  this->CheckCellsCareful->Reset();
                  this->CheckCellsCareful2->Reset();
                }
              }
              //Cell needs to be added to check list
              else
              {
                this->CheckCells2->InsertNextId(neighbors->GetId(j));
              }
            }

          }

        }
        //This statement is for if the start cell is a boundary cell
        else if (this->checkedcarefully[inputIndex][cellId] == 0 && start)
        {
          start=0;
          this->CheckCells->Reset();
          this->CheckCellsCareful->InsertNextId(cellId);
          if (fill)
          {
            this->FindRegionTipToe(inputIndex,fillnumber,1);
          }
          else
          {
            this->FindRegionTipToe(inputIndex,fillnumber,0);
          }
        }
      }

      //Swap the current check list to the full check list and continue
      tmp = this->CheckCells;
      this->CheckCells = this->CheckCells2;
      this->CheckCells2 = tmp;
      tmp->Reset();
    }
    return 1;
}

//This is the slow version of the flood fill algorithm that is initiated
//when we get close to a boundary to ensure we don't cross the line
int vtkBooleanOperationPolyDataFilterOld::Impl::FindRegionTipToe(
    int inputIndex, int fillnumber,int fill)
{
    //Variables used in function
    int i;
    double boundarypt[3];
    double pt1[3];
    double pt2[3];
    vtkIdType j,k,l;
    vtkIdType *pts = 0;
    vtkIdType npts = 0;
    vtkIdType cellId;
    vtkIdType numNei, nei, p1, p2, nIds, neiId;

    //Id List to store neighbor cells for each set of nodes and a cell
    vtkSmartPointer<vtkIdList> tmp = vtkSmartPointer<vtkIdList>::New();
    vtkSmartPointer<vtkIdList> neiIds = vtkSmartPointer<vtkIdList>::New();

    //Variable for accessing neiIds list
    vtkIdType sz = 0;

    //Variables for the boundary cells adjacent to the boundary point
    vtkSmartPointer<vtkIdList> bLinesOne = vtkSmartPointer<vtkIdList>::New();
    vtkSmartPointer<vtkIdList> bLinesTwo = vtkSmartPointer<vtkIdList>::New();

    vtkIdType numCheckCells;
    //Get neighboring cell for each pair of points in current cell
    //While there are still cells to be checked
    while ((numCheckCells = this->CheckCellsCareful->GetNumberOfIds()) > 0)
    {
      for (int c=0;c<numCheckCells;c++)
      {
        neiIds->Reset();
        cellId = this->CheckCellsCareful->GetId(c);
        //Get the three points of the cell
        this->Mesh[inputIndex]->GetCellPoints(cellId,npts,pts);
        if (this->checkedcarefully[inputIndex][cellId] == 0)
        {
          //Update this cell to have been checked carefully and assign it
          //with the fillnumber scalar
          if (fill)
            this->BooleanArray[inputIndex]->InsertValue(cellId,fillnumber);
          this->checkedcarefully[inputIndex][cellId] = 1;
          //For each edge of the cell
          //std::cout<<"Checking edges of cell "<<cellId<<endl;
          for (i=0; i < npts; i++)
          {
            p1 = pts[i];
            p2 = pts[(i+1)%(npts)];

            vtkSmartPointer<vtkIdList> neighbors =
              vtkSmartPointer<vtkIdList>::New();
            //Initial check to make sure the cell is in fact a face cell
            this->Mesh[inputIndex]->
              GetCellEdgeNeighbors(cellId,p1,p2,neighbors);
            numNei = neighbors->GetNumberOfIds();

            //Check to make sure it is an oustide surface cell,
            //i.e. one neighbor
            if (numNei==1)
            {
              int count = 0;
                //Check to see if cell is on the boundary,
                //if it is get adjacent lines
              if (this->BoundaryPointArray[inputIndex]->GetValue(p1) == 1)
                count++;

              if (this->BoundaryPointArray[inputIndex]->GetValue(p2) == 1)
                count++;

              nei=neighbors->GetId(0);
              //if cell is not on the boundary, add new cell to check list
              if (count < 2)
              {
                neiIds->InsertNextId(nei);
              }
              //if cell is on boundary, check to make sure it isn't
              //false positive; don't add to check list. This is done by
              //getting the boundary lines attached to each point, then
              //intersecting the two lists. If the result is zero, then this
              //is a false positive
              else
              {
                vtkIdType bPt1 = pointMapper[inputIndex][p1];
                this->IntersectionLines->GetPointCells(bPt1,bLinesOne);

                vtkIdType bPt2 = pointMapper[inputIndex][p2];
                this->IntersectionLines->GetPointCells(bPt2,bLinesTwo);

                bLinesOne->IntersectWith(bLinesTwo);
                //Cell is false positive. Add to check list.
                if (bLinesOne->GetNumberOfIds() == 0)
                {
                  //std::cout<<"False positive! "<<nei<<endl;
                  neiIds->InsertNextId(nei);
                }
                //else
                  //std::cout<<"I have not been added because false"<<endl;
              }
            }
            else
            {
              //cout<<"NumNei is not 1"<<endl;
              //cout<<"Number of Neighbors "<<numNei<<endl;
              //cout<<"Cell is "<<cellId<<endl;
              for (k=0;k<numNei;k++)
              {
                //cout<<"Id!!! "<<neighbors->GetId(k)<<endl;
              }
            }
          }

          nIds = neiIds->GetNumberOfIds();
          if (nIds>0)
          {
            //Add all Ids in current list to global list of Ids
            for (k=0; k< nIds;k++)
            {
                neiId = neiIds->GetId(k);
                if (this->checkedcarefully[inputIndex][neiId]==0)
                {
                  this->CheckCellsCareful2->InsertNextId(neiId);
                }
                else if (this->checked[inputIndex][neiId]==0)
                {
                  this->CheckCells2->InsertNextId(neiId);
                }
            }
          }
        }
      }

      //Add current list of checked cells to the full list and continue
      tmp = this->CheckCellsCareful;
      this->CheckCellsCareful = this->CheckCellsCareful2;
      this->CheckCellsCareful2 = tmp;
      tmp->Reset();
    }
    return 1;
}

void vtkBooleanOperationPolyDataFilterOld::Impl::Initialize()
{
  int numPts;
  int numPolys;
  int numLinePts;
  for (int i =0;i<2;i++)
  {
    if (this->Mesh[i]->GetNumberOfPoints() == 0 ||
        this->Mesh[i]->GetNumberOfCells() == 0)
    {
    vtkGenericWarningMacro( << "Mesh has zero points or cells and "<<
        "cannot run filter");
      return;
    }

    //Get the number of Polys for scalar  allocation
    numPolys = this->Mesh[i]->GetNumberOfPolys();
    numPts = this->Mesh[i]->GetNumberOfPoints();
    numLinePts = this->IntersectionLines->GetNumberOfPoints();

    //Allocate space for each Boundary Array and the fill array
    this->BoundaryPointArray[i]->SetNumberOfTuples(numPts);
    this->BoundaryCellArray[i]->SetNumberOfTuples(numPolys);
    this->BooleanArray[i]->SetNumberOfTuples(numPolys);
    this->checked[i] = new vtkIdType[numPolys];
    this->checkedcarefully[i] = new vtkIdType[numPolys];
    this->pointMapper[i] = new vtkIdType[numPolys];
    this->reversePointMapper[i] = new vtkIdType[numLinePts];

    for (int j=0;j<numPts;j++)
    {
      this->BoundaryPointArray[i]->InsertValue(j,0);
    }
    for (int j=0;j<numPolys;j++)
    {
      this->BoundaryCellArray[i]->InsertValue(j,0);
      this->BooleanArray[i]->InsertValue(j,0);
      this->checked[i][j] = 0;
      this->checkedcarefully[i][j] = 0;
      this->pointMapper[i][j] = -1;
    }
    for (int j=0;j<numLinePts;j++)
    {
      this->reversePointMapper[i][j] = -1;
    }
  }
  this->NewCellIds[0]->DeepCopy(this->IntersectionLines->GetCellData()->
      GetArray("NewCell0ID"));
  this->NewCellIds[1]->DeepCopy(this->IntersectionLines->GetCellData()->
      GetArray("NewCell1ID"));

  this->BooleanArray[0]->SetName("BooleanRegion");
  this->BooleanArray[1]->SetName("BooleanRegion");
  this->Mesh[0]->GetCellData()->AddArray(this->BooleanArray[0]);
  this->Mesh[0]->GetCellData()->SetActiveScalars("BooleanRegion");
  this->Mesh[1]->GetCellData()->AddArray(this->BooleanArray[1]);
  this->Mesh[1]->GetCellData()->SetActiveScalars("BooleanRegion");

  this->BoundaryCellArray[0]->SetName("BoundaryCells");
  this->BoundaryCellArray[1]->SetName("BoundaryCells");
  this->Mesh[0]->GetCellData()->AddArray(this->BoundaryCellArray[0]);
  this->Mesh[0]->GetCellData()->SetActiveScalars("BoundaryCells");
  this->Mesh[1]->GetCellData()->AddArray(this->BoundaryCellArray[1]);
  this->Mesh[1]->GetCellData()->SetActiveScalars("BoundaryCells");

  this->BoundaryPointArray[0]->SetName("BoundaryPoints");
  this->BoundaryPointArray[1]->SetName("BoundaryPoints");
  this->Mesh[0]->GetPointData()->AddArray(this->BoundaryPointArray[0]);
  this->Mesh[0]->GetPointData()->SetActiveScalars("BoundaryPoints");
  this->Mesh[1]->GetPointData()->AddArray(this->BoundaryPointArray[1]);
  this->Mesh[1]->GetPointData()->SetActiveScalars("BoundaryPoints");
}

//Function to find the regions on each input separated by the intersection
//lines
void vtkBooleanOperationPolyDataFilterOld::Impl::GetBooleanRegions(
    int inputIndex,std::vector<simLoop> *loops)
{
  vtkIdType nextCell;
  vtkIdType p1,p2;
  vtkIdType outputCellId0;
  vtkIdType outputCellId1;
  vtkSmartPointer<vtkPolyData> tmpPolyData =
    vtkSmartPointer<vtkPolyData>::New();
  tmpPolyData->DeepCopy(this->Mesh[inputIndex]);
  tmpPolyData->BuildLinks();

  std::vector<simLoop>::iterator loopit;
  std::list<simLine>::iterator cellit;

  //For each intersection loop
  for (loopit = loops->begin();loopit != loops->end(); ++loopit)
  {
    std::list<simLine> loopcells = (loopit)->cells;
    //Go through each cell in the loop
    for (cellit=loopcells.begin();cellit != loopcells.end();++cellit)
    {
      simLine nextLine;
      nextLine = *cellit;
      nextCell = nextLine.id; p1 = nextLine.pt1; p2 = nextLine.pt2;
      outputCellId0 = this->NewCellIds[inputIndex]->GetComponent(nextCell,0);
      outputCellId1 = this->NewCellIds[inputIndex]->GetComponent(nextCell,1);
      //If the cell has not been given an orientation from the flood fill
      //algorithm and it has an id from vtkIntersectionPolyDataFilterOld
      if (outputCellId0 != -1)
      {
        //If the cell hasn't been touched
        if (this->checkedcarefully[inputIndex][outputCellId0] == 0)
        {
          int sign1 = this->GetCellOrientation(tmpPolyData,outputCellId0,
            p1,p2,inputIndex);
          //If cell orientation is found
          if (sign1 != 0)
          {
            this->CheckCells->InsertNextId(outputCellId0);
            this->FindRegion(inputIndex,sign1,1,1);
            this->CheckCells->Reset();
            this->CheckCells2->Reset();
            this->CheckCellsCareful->Reset();
            this->CheckCellsCareful2->Reset();
          }
        }
      }
      //Check cell on other side of intersection line
      if (outputCellId1 != -1)
      {
        //If the cell hasn't been touched
        if (this->checkedcarefully[inputIndex][outputCellId1] == 0)
        {
          int sign2 = this->GetCellOrientation(tmpPolyData,outputCellId1,
              p1,p2,inputIndex);
          //If cell orientation is found
          if (sign2 != 0)
          {
            this->CheckCells->InsertNextId(outputCellId1);
            this->FindRegion(inputIndex,sign2,1,1);
            this->CheckCells->Reset();
            this->CheckCells2->Reset();
            this->CheckCellsCareful->Reset();
            this->CheckCellsCareful2->Reset();
          }
        }
      }
    }
  }
}

//Get Cell orientation so we know which value to flood fill a region with
int vtkBooleanOperationPolyDataFilterOld::Impl::GetCellOrientation(
    vtkPolyData *pd,vtkIdType cellId, vtkIdType p0, vtkIdType p1, int index)
{
  vtkIdType npts;
  vtkIdType *pts;
  vtkIdType cellPtId0,cellPtId1,cellPtId2;
    //std::cout<<"CELL!!!! "<<cellId<<endl;
    pd->BuildLinks();
  pd->GetCellPoints(cellId,npts,pts);
    //std::cout<<"After points"<<endl;
  vtkSmartPointer<vtkPoints> cellPts =
    vtkSmartPointer<vtkPoints>::New();
  vtkSmartPointer<vtkPolyData> cellPD =
    vtkSmartPointer<vtkPolyData>::New();
  vtkSmartPointer<vtkCellArray> cellLines =
    vtkSmartPointer<vtkCellArray>::New();
  //pt0Id and pt1Id are from intersectionLines PolyData and I am trying
  //to compare these to the point ids in pd.
  cellPtId0 = this->reversePointMapper[index][p0];
  cellPtId1 = this->reversePointMapper[index][p1];
  double points[3][3];
  for (int j=0;j<npts;j++)
  {
    pd->GetPoint(pts[j],points[j]);
    if(cellPtId0 != pts[j] && cellPtId1 != pts[j])
    {
      cellPtId2 = pts[j];
    }
  }
  cellPts->InsertNextPoint(pd->GetPoint(cellPtId0));
  cellPts->InsertNextPoint(pd->GetPoint(cellPtId1));
  cellPts->InsertNextPoint(pd->GetPoint(cellPtId2));
  cellPD->SetPoints(cellPts);
  for (int j=0;j<npts;j++)
  {
    int spot1 = j;
    int spot2 = (j+1)%3;
    cellLines->InsertNextCell(2);
    cellLines->InsertCellPoint(spot1);
    cellLines->InsertCellPoint(spot2);
  }
  cellPD->SetLines(cellLines);

  // Set up a transform that will rotate the points to the
  // XY-plane (normal aligned with z-axis).
  vtkSmartPointer< vtkTransform > transform =
    vtkSmartPointer< vtkTransform >::New();
  double zaxis[3] = {0, 0, 1};
  double rotationAxis[3], normal[3], center[3], rotationAngle;

  vtkTriangle::ComputeNormal(points[0], points[1], points[2], normal);

  double dotZAxis = vtkMath::Dot( normal, zaxis );
  if ( fabs(1.0 - dotZAxis) < 1e-6 )
    {
    // Aligned with z-axis
    rotationAxis[0] = 1.0;
    rotationAxis[1] = 0.0;
    rotationAxis[2] = 0.0;
    rotationAngle = 0.0;
    }
  else if ( fabs( 1.0 + dotZAxis ) < 1e-6 )
    {
    // Co-linear with z-axis, but reversed sense.
    // Aligned with z-axis
    rotationAxis[0] = 1.0;
    rotationAxis[1] = 0.0;
    rotationAxis[2] = 0.0;
    rotationAngle = 180.0;
    }
  else
    {
    // The general case
    vtkMath::Cross(normal, zaxis, rotationAxis);
    vtkMath::Normalize(rotationAxis);
    rotationAngle =
      vtkMath::DegreesFromRadians(acos(vtkMath::Dot(zaxis, normal)));
    }

  transform->PreMultiply();
  transform->Identity();

  //std::cout<<"ROTATION ANGLE "<<rotationAngle<<endl;
  transform->RotateWXYZ(rotationAngle,
                        rotationAxis[0],
                        rotationAxis[1],
                        rotationAxis[2]);

  vtkTriangle::TriangleCenter(points[0], points[1], points[2], center);
  transform->Translate(-center[0], -center[1], -center[2]);

  vtkSmartPointer<vtkTransformPolyDataFilter> transformer =
    vtkSmartPointer<vtkTransformPolyDataFilter>::New();
  transformer->SetInputData(cellPD);
  transformer->SetTransform(transform);
  transformer->Update();

  vtkSmartPointer<vtkPolyData> transPD =
    vtkSmartPointer<vtkPolyData>::New();
  transPD = transformer->GetOutput();
  transPD->BuildLinks();

  double area = 0;
  double tedgept1[3];
  double tedgept2[3];
  vtkIdType newpt;
  for(newpt=0;newpt<transPD->GetNumberOfPoints()-1;newpt++)
  {
      transPD->GetPoint(newpt,tedgept1);
      transPD->GetPoint(newpt+1,tedgept2);
      area = area + (tedgept1[0]*tedgept2[1])-(tedgept2[0]*tedgept1[1]);
  }
  transPD->GetPoint(newpt,tedgept1);
  transPD->GetPoint(0,tedgept2);
  area = area + (tedgept1[0]*tedgept2[1])-(tedgept2[0]*tedgept1[1]);

  int value=0;
  double tolerance = 1e-6;
  if (area < 0 && fabs(area) > tolerance)
  {
    value = -1;
  }
  else if (area > 0 && fabs(area) > tolerance)
  {
    value = 1;
  }
  else
  {
    if (this->Verbose)
    {
      std::cout<<"Line pts are "<<p0<<" and "<<p1<<endl;
      std::cout<<"PD pts are "<<cellPtId0<<" and "<<cellPtId1<<endl;
      std::cout<<"Cell pts are ";

      for (int j=0;j<npts;j++)
      {
        std::cout<<pts[j]<<" ";
      }
      std::cout<<endl;
    }
    value = 0;
  }

  return value;
}

//Reset the find region arrays to test another region
void vtkBooleanOperationPolyDataFilterOld::Impl::ResetCheckArrays()
{
  vtkIdType pointId,cellId;
  for (int i=0;i<2;i++)
  {
    int numPolys = this->Mesh[i]->GetNumberOfCells();
    for (cellId = 0; cellId < numPolys;cellId++)
    {
      if (this->BoundaryCellArray[i]->GetValue(cellId) == 1)
      {
        this->checked[i][cellId] = 1;
        this->checkedcarefully[i][cellId] = 0;
      }
      else
      {
        this->checked[i][cellId] = 0;
        this->checkedcarefully[i][cellId] = 1;
      }
    }
  }
}

//Set the boundary arrays on the mesh
void vtkBooleanOperationPolyDataFilterOld::Impl::SetBoundaryArrays()
{
  //Variables used in the function
  double pt[3];
  vtkIdType pointId,bp1,bp2,i;
  vtkSmartPointer<vtkIdList> bpCellIds1 =
    vtkSmartPointer<vtkIdList>::New();
  vtkSmartPointer<vtkIdList> bpCellIds2 =
    vtkSmartPointer<vtkIdList>::New();
  //Point locator to find points on mesh that are the points on the boundary
  //lines
  vtkSmartPointer<vtkPointLocator> pointLocator1 =
    vtkSmartPointer<vtkPointLocator>::New();
  vtkSmartPointer<vtkPointLocator> pointLocator2 =
    vtkSmartPointer<vtkPointLocator>::New();
  pointLocator1->SetDataSet(this->Mesh[0]);
  pointLocator1->BuildLocator();
  pointLocator2->SetDataSet(this->Mesh[1]);
  pointLocator2->BuildLocator();

  int numPoints = this->IntersectionLines->GetNumberOfPoints();

  for (pointId = 0;pointId < numPoints;pointId++)
  {
    this->IntersectionLines->GetPoint(pointId,pt);
    //Find point on mesh
    bp1 = pointLocator1->FindClosestPoint(pt);
    this->pointMapper[0][bp1] = pointId;
    this->reversePointMapper[0][pointId] = bp1;
    this->BoundaryPointArray[0]->InsertValue(bp1,1);
    this->Mesh[0]->GetPointCells(bp1,bpCellIds1);
    //Set the point mapping array
    //Assign each cell attached to this point as a boundary cell
    for (i = 0;i < bpCellIds1->GetNumberOfIds();i++)
    {
      this->BoundaryCellArray[0]->InsertValue(bpCellIds1->GetId(i),1);
      this->checked[0][bpCellIds1->GetId(i)] = 1;
    }

    bp2 = pointLocator2->FindClosestPoint(pt);
    this->pointMapper[1][bp2] = pointId;
    this->reversePointMapper[1][pointId] = bp2;
    this->BoundaryPointArray[1]->InsertValue(bp2,1);
    this->Mesh[1]->GetPointCells(bp2,bpCellIds2);
    //Set the point mapping array
    //Assign each cell attached to this point as a boundary cell
    for (i = 0;i < bpCellIds2->GetNumberOfIds();i++)
    {
      this->BoundaryCellArray[1]->InsertValue(bpCellIds2->GetId(i),1);
      this->checked[1][bpCellIds2->GetId(i)] = 1;
    }
  }
}

//Set the original finding region check arrays
void vtkBooleanOperationPolyDataFilterOld::Impl::SetCheckArrays()
{
  int numPts;
  int numPolys;
  for (int i =0;i<2;i++)
  {
    //Get the number of Polys for scalar  allocation
    numPolys = this->Mesh[i]->GetNumberOfPolys();
    numPts = this->Mesh[i]->GetNumberOfPoints();

    for (int j=0;j < numPolys;j++)
    {
      if (this->checked[i][j] == 0)
        this->checkedcarefully[i][j] = 1;
      else
        this->checkedcarefully[i][j] = 0;
    }
  }
}
//---------------------------------------------------------------------------

vtkStandardNewMacro(vtkBooleanOperationPolyDataFilterOld);

//-----------------------------------------------------------------------------
vtkBooleanOperationPolyDataFilterOld::vtkBooleanOperationPolyDataFilterOld() :
  vtkPolyDataAlgorithm()
{
  this->Operation = VTK_UNION;

  this->SetNumberOfInputPorts(2);
  this->SetNumberOfOutputPorts(2);
  this->NoIntersectionOutput = 1;

  this->NumberOfIntersectionPoints = 0;
  this->NumberOfIntersectionLines = 0;

  this->Verbose = 0;
  this->Status = 1;
  this->Tolerance = 1e-6;
}

//-----------------------------------------------------------------------------
vtkBooleanOperationPolyDataFilterOld::~vtkBooleanOperationPolyDataFilterOld()
{
}

//-----------------------------------------------------------------------------
int vtkBooleanOperationPolyDataFilterOld::RequestData(
    vtkInformation*        vtkNotUsed(request),
    vtkInformationVector** inputVector,
    vtkInformationVector*  outputVector)
{
  vtkInformation* inInfo0 = inputVector[0]->GetInformationObject(0);
  vtkInformation* inInfo1 = inputVector[1]->GetInformationObject(0);
  vtkInformation* outInfo0 = outputVector->GetInformationObject(0);
  vtkInformation* outInfo1 = outputVector->GetInformationObject(1);

  if (!inInfo0 || !inInfo1 || !outInfo0 || !outInfo1)
    {
    this->Status = 0;
    return 0;
    }

  vtkPolyData* input0 =
    vtkPolyData::SafeDownCast(inInfo0->Get(vtkDataObject::DATA_OBJECT()));
  vtkPolyData* input1 =
    vtkPolyData::SafeDownCast(inInfo1->Get(vtkDataObject::DATA_OBJECT()));

  vtkPolyData* outputSurface =
    vtkPolyData::SafeDownCast(outInfo0->Get(vtkDataObject::DATA_OBJECT()));
  vtkPolyData* outputIntersection =
    vtkPolyData::SafeDownCast(outInfo1->Get(vtkDataObject::DATA_OBJECT()));

  if (!input0 || !input1 || !outputSurface || !outputIntersection)
    {
    this->Status = 0;
    return 0;
    }

  // Get intersected versions
  vtkSmartPointer<vtkIntersectionPolyDataFilterOld> PolyDataIntersection =
    vtkSmartPointer<vtkIntersectionPolyDataFilterOld>::New();
  PolyDataIntersection->SetInputConnection
    (0, this->GetInputConnection(0, 0));
  PolyDataIntersection->SetInputConnection
    (1, this->GetInputConnection(1, 0));
  PolyDataIntersection->SplitFirstOutputOn();
  PolyDataIntersection->SplitSecondOutputOn();
  PolyDataIntersection->SetTolerance(this->Tolerance);
  PolyDataIntersection->Update();
  if (PolyDataIntersection->GetStatus() != 1)
    {
    this->Status = 0;
    return 0;
    }

  this->NumberOfIntersectionPoints =
          PolyDataIntersection->GetNumberOfIntersectionPoints();
  this->NumberOfIntersectionLines =
          PolyDataIntersection->GetNumberOfIntersectionLines();

  if (this->Verbose)
  {
    cout<<"Intersection is Done!!!"<<endl;
  }

  vtkBooleanOperationPolyDataFilterOld::Impl *impl =
    new vtkBooleanOperationPolyDataFilterOld::Impl();
  impl->Mesh[0]->DeepCopy(PolyDataIntersection->GetOutput(1));
  impl->Mesh[0]->BuildLinks();
  impl->Mesh[1]->DeepCopy(PolyDataIntersection->GetOutput(2));
  impl->Mesh[1]->BuildLinks();
  impl->IntersectionLines->DeepCopy(PolyDataIntersection->GetOutput(0));
  outputIntersection->DeepCopy(PolyDataIntersection->GetOutput(0));

  if (this->NumberOfIntersectionPoints == 0 ||
                  this->NumberOfIntersectionLines == 0)
  {
    vtkGenericWarningMacro( << "No intersections!");
    if (this->NoIntersectionOutput == 0)
    {
      delete impl;
      return 1;
    }
    else
    {
      int numPts;
      int numPolys;
      for (int i =0;i<2;i++)
      {
        //Get the number of Polys for scalar  allocation
        numPolys = impl->Mesh[i]->GetNumberOfPolys();
        numPts = impl->Mesh[i]->GetNumberOfPoints();
        for (int j=0;j<numPts;j++)
          impl->BoundaryPointArray[i]->InsertValue(j,0);

        for (int j=0;j<numPolys;j++)
          impl->BoundaryCellArray[i]->InsertValue(j,0);

        impl->BoundaryCellArray[i]->SetName("BoundaryCells");
        impl->Mesh[i]->GetCellData()->AddArray(impl->BoundaryCellArray[i]);
        impl->Mesh[i]->GetCellData()->SetActiveScalars("BoundaryCells");

        impl->BoundaryPointArray[i]->SetName("BoundaryPoints");
        impl->Mesh[i]->GetPointData()->AddArray(impl->BoundaryPointArray[i]);
        impl->Mesh[i]->GetPointData()->SetActiveScalars("BoundaryPoints");
      }
      if (this->NoIntersectionOutput == 1)
      {
        if (this->Verbose)
          std::cout<<"Only returning first surface"<<endl;
        outputSurface->DeepCopy(impl->Mesh[0]);
      }
      else if (this->NoIntersectionOutput == 2)
      {
        if (this->Verbose)
          std::cout<<"Only returning second surface"<<endl;
        outputSurface->DeepCopy(impl->Mesh[1]);
      }
      else
      {
        if (this->Verbose)
          std::cout<<"Keeping both"<<endl;

        vtkSmartPointer<vtkAppendPolyData> appender =
          vtkSmartPointer<vtkAppendPolyData>::New();
        appender->AddInputData(impl->Mesh[0]);
        appender->AddInputData(impl->Mesh[1]);
        appender->Update();
        outputSurface->DeepCopy(appender->GetOutput());
      }
      delete impl;
      return 1;
    }
  }

  double badtri1[2], badtri2[2];
  double freeedge1[2], freeedge2[2];
  impl->Mesh[0]->GetCellData()->GetArray("BadTri")->GetRange(badtri1,0);
  impl->Mesh[0]->GetCellData()->GetArray("FreeEdge")->GetRange(freeedge1,0);

  impl->Mesh[1]->GetCellData()->GetArray("BadTri")->GetRange(badtri2,0);
  impl->Mesh[1]->GetCellData()->GetArray("FreeEdge")->GetRange(freeedge2,0);

  //Set the check and boundary arrays for region finding
  if (this->Verbose)
    std::cout<<"Initializing"<<endl;
  impl->Initialize();
  if (this->Verbose)
    std::cout<<"Setting Bound Arrays"<<endl;
  impl->SetBoundaryArrays();
  if (this->Verbose)
    std::cout<<"Setting Check Arrays"<<endl;
  impl->SetCheckArrays();

  //Determine the intersection type and obtain the intersection loops
  //to give to Boolean Region finding
  if (this->Verbose)
    std::cout<<"Determining Intersection Type"<<endl;
  std::vector<simLoop> loops;
  impl->DetermineIntersection(&loops);

  //Get the regions bounded by the intersection lines and give correct
  //orientation
  impl->GetBooleanRegions(0,&loops);
  if (this->Verbose)
    std::cout<<"DONE WITH 1"<<endl;
  impl->GetBooleanRegions(1,&loops);
  if (this->Verbose)
    std::cout<<"DONE WITH 2"<<endl;
  WriteVTP("/Users/adamupdegrove/Documents/Research/Code/vtkTests/Tests/Input0.vtp",impl->Mesh[0],"Split");
  WriteVTP("/Users/adamupdegrove/Documents/Research/Code/vtkTests/Tests/Input1.vtp",impl->Mesh[1],"Split");

  //Combine certain orientations based on the operation desired
  impl->PerformBoolean(outputSurface,this->Operation);

  //Number of bad triangles and free edges (Should be zero for watertight,
  //manifold surfaces!
  if (this->Verbose)
    std::cout<<"SURFACE 1 BAD TRI MIN: "<<badtri1[0]<<" MAX: "<<
      badtri1[1]<<endl;
  if (this->Verbose)
    std::cout<<"SURFACE 1 FREE EDGE MIN: "<<freeedge1[0]<<" MAX: "<<
      freeedge1[1]<<endl;
  if (this->Verbose)
    std::cout<<"SURFACE 2 BAD TRI MIN: "<<badtri2[0]<<" MAX: "<<
      badtri2[1]<<endl;
  if (this->Verbose)
    std::cout<<"SURFACE 2 FREE EDGE MIN: "<<freeedge2[0]<<" MAX: "<<
      freeedge2[1]<<endl;

  double fullbadtri[2], fullfreeedge[2],dummy[2];
  vtkIntersectionPolyDataFilterOld::CleanAndCheckSurface(
      outputSurface,dummy,this->Tolerance);
  outputSurface->GetCellData()->GetArray("BadTri")->
    GetRange(fullbadtri,0);
  outputSurface->GetCellData()->GetArray("FreeEdge")->
    GetRange(fullfreeedge,0);

  //Add Normals
  vtkSmartPointer<vtkPolyDataNormals> normaler =
    vtkSmartPointer<vtkPolyDataNormals>::New();
  normaler->SetInputData(outputSurface);
  normaler->AutoOrientNormalsOn();
  normaler->Update();
  outputSurface->DeepCopy(normaler->GetOutput());

  if (this->Verbose)
  {
    std::cout<<"FULL SURFACE BAD TRI MIN: "<<fullbadtri[0];
    std::cout<<" MAX: "<<fullbadtri[1]<<endl;
    std::cout<<"FULL SURFACE FREE EDGE MIN: "<<fullfreeedge[0];
    std::cout<<" MAX: "<<fullfreeedge[1]<<endl;
  }

  delete impl;
  return 1;
}

//-----------------------------------------------------------------------------
void vtkBooleanOperationPolyDataFilterOld::PrintSelf(
    ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);

  os << indent << "Operation: ";
  switch ( this->Operation )
    {
    case VTK_UNION:
      os << "UNION";
      break;

    case VTK_INTERSECTION:
      os << "INTERSECTION";
      break;

    case VTK_DIFFERENCE:
      os << "DIFFERENCE";
      break;
    }
  os << "\n";
  os << indent << "No Intersection Output: " <<
          this->NoIntersectionOutput << "\n";
  os << indent << "Tolerance: " <<
          this->Tolerance << "\n";
  os << indent << "NumberOfIntersectionPoints: " <<
          this->NumberOfIntersectionPoints << "\n";
  os << indent << "NumberOfIntersectionLines: " <<
          this->NumberOfIntersectionLines << "\n";
}

//-----------------------------------------------------------------------------
int vtkBooleanOperationPolyDataFilterOld::FillInputPortInformation(
    int port, vtkInformation *info)
{
  if (!this->Superclass::FillInputPortInformation(port, info))
    {
    return 0;
    }
  if (port == 0)
    {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    }
  else if (port == 1)
    {
    info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPolyData");
    info->Set(vtkAlgorithm::INPUT_IS_OPTIONAL(), 0);
    }
  return 1;
}

//-----------------------------------------------------------------------------

//Determine type of intersection
void vtkBooleanOperationPolyDataFilterOld::Impl::DetermineIntersection(
    std::vector<simLoop> *loops)
{
  int caseId=0;
  vtkIdType interPt;
  vtkIdType nextCell;
  vtkSmartPointer<vtkIdList> cellIds = vtkSmartPointer<vtkIdList>::New();
  int numInterPts = this->IntersectionLines->GetNumberOfPoints();
  int numInterLines = this->IntersectionLines->GetNumberOfLines();

  bool *usedPt;
  usedPt = new bool[numInterPts];
  for (interPt=0;interPt<numInterPts;interPt++)
  {
    usedPt[interPt] = false;
  }

  for (interPt=0;interPt<numInterPts;interPt++)
  {
    if (usedPt[interPt] == false)
    {
      simLoop newloop;
      this->IntersectionLines->GetPointCells(interPt,cellIds);
      if (this->Verbose)
      {
        if (cellIds->GetNumberOfIds() > 2)
          std::cout<<"Number Of Cells is greater than 2 for first point "
            <<interPt<<endl;
        else if (cellIds->GetNumberOfIds() < 2)
          std::cout<<"Number Of Cells is less than 2 for point "<<interPt
            <<endl;
      }

      nextCell = cellIds->GetId(0);

      //Run through intersection lines to get loops!
      newloop.startPt = interPt;
      caseId = this->RunLoopFind(interPt,nextCell,usedPt,&newloop);
      if (caseId != -1)
      {
        //If the intersection loop is open
        if (this->intersectionCase == 2)
        {
          vtkIdType nextPt = caseId;
          if (this->Verbose)
            std::cout<<"End point of open loop is "<<nextPt<<endl;
          newloop.endPt = nextPt;
          newloop.loopType = 2;
          nextCell = cellIds->GetId(1);
          vtkIdType newId = this->RunLoopFind(interPt,nextCell,usedPt,
                                              &newloop);
          newloop.startPt = newId;
          //Save start and end point in custom data structure for loop
        }
        else
        {
          newloop.loopType = 1;
        }
      }
      usedPt[interPt] = true;
      loops->push_back(newloop);
    }
  }
  if (this->Verbose)
    std::cout<<"Number Of Loops: "<<loops->size()<<endl;

  delete [] usedPt;
}

//Function for if the intersection is soft closed or open
int vtkBooleanOperationPolyDataFilterOld::Impl::RunLoopFind(
    vtkIdType interPt,vtkIdType nextCell, bool *usedPt,simLoop *loop)
{
  vtkIdType prevPt = interPt;
  vtkIdType nextPt = interPt;
  vtkSmartPointer<vtkIdList> pointIds = vtkSmartPointer<vtkIdList>::New();
  vtkSmartPointer<vtkIdList> cellIds = vtkSmartPointer<vtkIdList>::New();

  IntersectionLines->GetCellPoints(nextCell,pointIds);
  if (this->Verbose)
  {
    if (pointIds->GetNumberOfIds() > 2)
      std::cout<<"Number Of Points is greater than 2 for first cell "
        <<nextCell<<endl;
    else if (pointIds->GetNumberOfIds() < 2)
      std::cout<<"Number Of Points is less than 2 for first cell "
        <<nextCell<<endl;
  }

  if (pointIds->GetId(0) == nextPt)
    nextPt = pointIds->GetId(1);
  else
    nextPt = pointIds->GetId(0);
  simLine newline;
  newline.pt1 = prevPt;
  newline.pt2 = nextPt;
  newline.id = nextCell;
  loop->cells.push_back(newline);

  usedPt[nextPt] = true;
  while(nextPt != loop->cells.front().pt1)
  {
    IntersectionLines->GetPointCells(nextPt,cellIds);
    if (cellIds->GetNumberOfIds() > 2)
    {
      intersectionCase = 1;
      if (this->Verbose)
        std::cout<<"Number Of Cells is greater than 2 for point "
          <<nextPt<<endl;
      usedPt[nextPt] = false;
      nextCell = this->RunLoopTest(nextPt,nextCell,loop,usedPt);
      if (nextCell == -1)
        break;

      if (this->Verbose)
        std::cout<<"Next cell is "<<nextCell<<endl;
    }
    else if (cellIds->GetNumberOfIds() < 2)
    {
      intersectionCase = 2;
      if (this->Verbose)
        std::cout<<"Number Of Cells is less than 2 for point "<<nextPt<<endl;
      return nextPt;
    }
    else
    {
      if (cellIds->GetId(0) == nextCell)
        nextCell = cellIds->GetId(1);
      else
        nextCell = cellIds->GetId(0);
    }

    IntersectionLines->GetCellPoints(nextCell,pointIds);
    if (this->Verbose)
    {
      if (pointIds->GetNumberOfIds() > 2)
        std::cout<<"Number Of Points is greater than 2 for cell "
          <<nextCell<<endl;
      else if (pointIds->GetNumberOfIds() < 2)
        std::cout<<"Number Of Points is less than 2 for first cell "
          <<nextCell<<endl;
    }
    prevPt = nextPt;
    if (pointIds->GetId(0) == nextPt)
      nextPt = pointIds->GetId(1);
    else
      nextPt = pointIds->GetId(0);
    usedPt[nextPt] = true;

    simLine newestline;
    newestline.pt1 = prevPt;
    newestline.pt2 = nextPt;
    newestline.id = nextCell;
    loop->cells.push_back(newestline);
  }
  loop->endPt = nextPt;
  loop->loopType = 0;
  //std::cout<<"Start and End Point are "<<nextPt<<endl;

  return -1;
}

//Tests an orientation in a specified region
int vtkBooleanOperationPolyDataFilterOld::Impl::RunLoopTest(
    vtkIdType interPt, vtkIdType nextCell, simLoop *loop, bool *usedPt)
{
  //This test is only if the intersection has soft closed loops
  if (this->Verbose)
    std::cout<<"Running Loop Test to find right loop"<<endl;
  int input = 0;
  vtkIdType p1,p2;
  vtkIdType stopCell = nextCell;
  vtkIdType prevPt = interPt;
  vtkIdType nextPt = interPt;
  vtkIdType outputCellId0,outputCellId1;
  vtkSmartPointer<vtkIdList> pointIds = vtkSmartPointer<vtkIdList>::New();
  vtkSmartPointer<vtkIdList> cellIds = vtkSmartPointer<vtkIdList>::New();
  vtkSmartPointer<vtkPolyData> tmpPolyData =
    vtkSmartPointer<vtkPolyData>::New();
  tmpPolyData->DeepCopy(this->Mesh[input]);
  tmpPolyData->BuildLinks();
  std::list<simLine>::iterator cellit;

  IntersectionLines->GetPointCells(nextPt,cellIds);
  //std::cout<<"Number of cells should be more than two!! "<<
  //cellIds->GetNumberOfIds()<<endl;
  for (vtkIdType i=0;i<cellIds->GetNumberOfIds();i++)
  {
    int numRegionsFound=0;
    vtkIdType cellId = cellIds->GetId(i);
    if (this->Verbose)
      std::cout<<"Testing cell "<<cellId<<endl;
    IntersectionLines->GetCellPoints(cellId,pointIds);
    if (this->Verbose)
    {
      if (pointIds->GetNumberOfIds() > 2)
      {
        if (this->Verbose)
        {
          std::cout<<"Number Of Points is greater than 2 for first cell "<<
            nextCell<<endl;
        }
      }
      else if (pointIds->GetNumberOfIds() < 2)
      {
        if (this->Verbose)
        {
          std::cout<<"Number Of Points is less than 2 for first cell "<<
            nextCell<<endl;
        }
      }
    }

    if (pointIds->GetId(0) == interPt)
      nextPt = pointIds->GetId(1);
    else
      nextPt = pointIds->GetId(0);

    if (this->Verbose)
    {
      if (usedPt[nextPt] == true)
      {
        if (this->Verbose)
        {
          std::cout<<"Bad One"<<endl;
        }
      }
    }
    if (cellId != stopCell && usedPt[nextPt] != true)
    {
      simLine newline;
      newline.id = cellId;
      newline.pt1 = prevPt;
      newline.pt2 = nextPt;
      loop->cells.push_back(newline);
      //std::cout<<"Cell id is: "<<cellId<<endl;
      for (cellit=loop->cells.begin();cellit != loop->cells.end(); ++cellit)
      {
        simLine nextLine;
        nextLine = *cellit;
        nextCell = nextLine.id; p1 = nextLine.pt1; p2 = nextLine.pt2;
        //std::cout<<"Line cell is "<<nextCell<<endl;
        outputCellId0 = this->NewCellIds[input]->GetComponent(nextCell,0);
        outputCellId1 = this->NewCellIds[input]->GetComponent(nextCell,1);
        if (outputCellId0 != -1)
        {
          if (this->checkedcarefully[input][outputCellId0] == 0)
          {
            int sign1 = this->GetCellOrientation(tmpPolyData,outputCellId0,
              p1,p2,input);
            if (sign1 == -1)
            {
              numRegionsFound++;
              this->CheckCells->InsertNextId(outputCellId0);
              this->FindRegion(input,sign1,1,0);
              this->CheckCells->Reset();
              this->CheckCells2->Reset();
              this->CheckCellsCareful->Reset();
              this->CheckCellsCareful2->Reset();
            }
          }
        }
        if (outputCellId1 != -1)
        {
          if (this->checkedcarefully[input][outputCellId1] == 0)
          {
            int sign2 = this->GetCellOrientation(tmpPolyData,outputCellId1,
              p1,p2,input);
            if (sign2 == -1)
            {
              numRegionsFound++;
              this->CheckCells->InsertNextId(outputCellId1);
              this->FindRegion(input,sign2,1,0);
              this->CheckCells->Reset();
              this->CheckCells2->Reset();
              this->CheckCellsCareful->Reset();
              this->CheckCellsCareful2->Reset();
            }
          }
        }
      }
      loop->cells.pop_back();
      this->ResetCheckArrays();
      if (this->Verbose)
        std::cout<<"Number of Regions Found: "<<numRegionsFound<<endl;
      if (numRegionsFound == 1)
      {
        if (this->Verbose)
          std::cout<<"Legitimate Loop found"<<endl;
        return cellId;
      }
    }
  }
  //std::cout<<"Start and End Point are "<<nextPt<<endl;

  return -1;

}

//Combine the correct regions for output boolean
void vtkBooleanOperationPolyDataFilterOld::Impl::PerformBoolean(
    vtkPolyData *output,int Operation)
{
  vtkSmartPointer<vtkThreshold> thresholder =
    vtkSmartPointer<vtkThreshold>::New();
  vtkSmartPointer<vtkDataSetSurfaceFilter> surfacer =
    vtkSmartPointer<vtkDataSetSurfaceFilter>::New();

  vtkPolyData *surfaces[4];
  for (int i=0;i<4;i++)
  {
    surfaces[i] = vtkPolyData::New();;
  }

  //this->ThresholdRegions(surfaces);
  thresholder->SetInputData(this->Mesh[0]);
  thresholder->SetInputArrayToProcess(0, 0, 0, 1, "BooleanRegion");
  thresholder->ThresholdBetween(-1, -1);
  thresholder->Update();
  surfacer->SetInputData(thresholder->GetOutput());
  surfacer->Update();
  surfaces[0]->DeepCopy(surfacer->GetOutput());

  thresholder->SetInputData(this->Mesh[0]);
  thresholder->SetInputArrayToProcess(0, 0, 0, 1, "BooleanRegion");
  thresholder->ThresholdBetween(1, 1);
  thresholder->Update();
  surfacer->SetInputData(thresholder->GetOutput());
  surfacer->Update();
  surfaces[1]->DeepCopy(surfacer->GetOutput());

  thresholder->SetInputData(this->Mesh[1]);
  thresholder->SetInputArrayToProcess(0, 0, 0, 1, "BooleanRegion");
  thresholder->ThresholdBetween(1, 1);
  thresholder->Update();
  surfacer->SetInputData(thresholder->GetOutput());
  surfacer->Update();
  surfaces[2]->DeepCopy(surfacer->GetOutput());

  thresholder->SetInputData(this->Mesh[1]);
  thresholder->SetInputArrayToProcess(0, 0, 0, 1, "BooleanRegion");
  thresholder->ThresholdBetween(-1, -1);
  thresholder->Update();
  surfacer->SetInputData(thresholder->GetOutput());
  surfacer->Update();
  surfaces[3]->DeepCopy(surfacer->GetOutput());

  vtkSmartPointer<vtkAppendPolyData> appender =
    vtkSmartPointer<vtkAppendPolyData>::New();

  //If open intersection case, make sure correct region is taken
  if (this->intersectionCase == 2)
  {
    vtkSmartPointer<vtkPolyData> tmp =
      vtkSmartPointer<vtkPolyData>::New();
    int numCells[4];
    std::list<vtkIdType> nocellregion;
    for (int i=0;i<4;i++)
    {
      numCells[i] = surfaces[i]->GetNumberOfCells();
      if (numCells[i] == 0)
      {
        nocellregion.push_back(i);
      }
    }
    if (nocellregion.size() > 0)
    {
      if (nocellregion.front() == 0)
      {
        tmp->DeepCopy(surfaces[1]);
        surfaces[1]->DeepCopy(surfaces[0]);
        surfaces[0]->DeepCopy(tmp);
      }
      if (nocellregion.back() == 2)
      {
        tmp->DeepCopy(surfaces[3]);
        surfaces[3]->DeepCopy(surfaces[2]);
        surfaces[2]->DeepCopy(tmp);
      }
    }
  }
  if (Operation == 0)
  {
    appender->AddInputData(surfaces[0]);
    appender->AddInputData(surfaces[2]);
  }
  if (Operation == 1)
  {
    appender->AddInputData(surfaces[1]);
    appender->AddInputData(surfaces[3]);
  }
  if (Operation == 2)
  {
    appender->AddInputData(surfaces[0]);
    appender->AddInputData(surfaces[3]);
  }
  appender->Update();

  output->DeepCopy(appender->GetOutput());

  for (int i =0;i < 4;i++)
    surfaces[i]->Delete();
}

void vtkBooleanOperationPolyDataFilterOld::Impl::ThresholdRegions(vtkPolyData **surfaces)
{
  vtkPoints    *points[4];
  vtkCellArray *cells[4];
  vtkIntArray  *boundaryPoints[4];
  vtkIntArray  *boundaryCells[4];
  vtkIntArray  *booleanCells[4];

  for (int i=0;i<4;i++)
    {
      points[i] = vtkPoints::New();
      cells[i]  = vtkCellArray::New();
      boundaryPoints[i] = vtkIntArray::New();
      boundaryCells[i]  =  vtkIntArray::New();
      booleanCells[i]   = vtkIntArray::New();
    }

  for (int i=0; i<2; i++)
    {
    int numCells = this->Mesh[i]->GetNumberOfCells();
    for (int j=0; j<numCells; j++)
      {
      int value = this->BooleanArray[i]->GetValue(j);
      vtkIdType npts, *pts;
      this->Mesh[i]->GetCellPoints(j, npts, pts);
      if (value < 0)
        {
        vtkSmartPointer<vtkIdList> newPointIds = vtkSmartPointer<vtkIdList>::New();
        newPointIds->SetNumberOfIds(3);
        for (int k=0; k<npts; k++)
          {
          double pt[3];
          this->Mesh[i]->GetPoint(pts[k], pt);
          vtkIdType newId = points[3*i]->InsertNextPoint(pt);
          newPointIds->SetId(k, newId);
          boundaryPoints[3*i]->InsertValue(newId, this->BoundaryPointArray[i]->GetValue(pts[k]));
          }
        vtkIdType cellId = cells[3*i]->InsertNextCell(newPointIds);
        boundaryCells[3*i]->InsertValue(cellId, this->BoundaryCellArray[i]->GetValue(j));
        booleanCells[3*i]->InsertValue(cellId, this->BooleanArray[i]->GetValue(j));
        }
      if (value > 0)
        {
        vtkSmartPointer<vtkIdList> newPointIds = vtkSmartPointer<vtkIdList>::New();
        newPointIds->SetNumberOfIds(3);
        for (int k=0; k<npts; k++)
          {
          double pt[3];
          this->Mesh[i]->GetPoint(pts[k], pt);
          vtkIdType newId = points[i+1]->InsertNextPoint(pt);
          newPointIds->SetId(k, newId);
          boundaryPoints[i+1]->InsertValue(newId, this->BoundaryPointArray[i]->GetValue(pts[k]));
          }
        vtkIdType cellId = cells[i+1]->InsertNextCell(newPointIds);
        boundaryCells[i+1]->InsertValue(cellId, this->BoundaryCellArray[i]->GetValue(j));
        booleanCells[i+1]->InsertValue(cellId, this->BooleanArray[i]->GetValue(j));
        }
      }
    }
  for (int i=0; i<4; i++)
    {
    surfaces[i]->SetPoints(points[i]);
    surfaces[i]->SetPolys(cells[i]);
    surfaces[i]->BuildLinks();
    boundaryPoints[i]->SetName("BoundaryPoints");
    surfaces[i]->GetPointData()->AddArray(boundaryPoints[i]);
    boundaryCells[i]->SetName("BoundaryCells");
    surfaces[i]->GetCellData()->AddArray(boundaryCells[i]);
    booleanCells[i]->SetName("BooleanRegion");
    surfaces[i]->GetCellData()->AddArray(booleanCells[i]);

    points[i]->Delete();
    cells[i]->Delete();
    boundaryPoints[i]->Delete();
    boundaryCells[i]->Delete();
    booleanCells[i]->Delete();
    }
}
