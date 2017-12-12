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

#include "vtkCellData.h"
#include "vtkCleanPolyData.h"
#include "vtkDataArray.h"
#include "vtkDataWriter.h"
#include "vtkInformation.h"
#include "vtkPointData.h"
#include "vtkPolyData.h"
#include "vtkSmartPointer.h"
#include "vtkSVGlobals.h"
#include "vtkSVIOUtils.h"
#include "vtkSVGroupsSegmenter.h"

int main(int argc, char *argv[])
{
  // BEGIN PROCESSING COMMAND-LINE ARGUMENTS
  // Assume no options specified at command line
  bool RequestedHelp       = false;
  bool InputProvided       = false;
  bool OutputProvided      = false;
  bool CenterlinesProvided = false;

  // Variables used in processing the commandline
  int iarg, arglength;
  std::string tmpstr;

  // Filenames
  std::string inputFilename;
  std::string outputFilename;
  std::string centerlinesFilename;

  // Default values for options
  int useRadiusInfo = 1;
  int useVmtkClipping = 0;
  int enforceBoundaryDirections = 1;
  int polycubeDivisions = 5;
  int isVasculature = 1;
  int numberOfCenterlineRemovePts = 3;
  int writeCenterlineGraph = 0;
  int writeMergedCenterlines = 0;
  int writePolycubePd = 0;
  int writePolycubeUg = 0;
  int writeFinalHexMesh = 0;
  int writeAll = 0;
  int boundaryEnforceFactor = 0;

  double polycubeUnitLength = 0.0;
  double normalsWeighting = 0.8;
  double clipValue = 0.0;
  double cutoffRadiusFactor = VTK_SV_LARGE_DOUBLE;
  std::string groupIdsArrayName = "GroupIds";
  std::string radiusArrayName   = "MaximumInscribedSphereRadius";
  std::string blankingArrayName = "Blanking";

  // argc is the number of strings on the command-line
  //  starting with the program name
  for(iarg=1; iarg<argc; iarg++){
      arglength = strlen(argv[iarg]);
      // replace 0..arglength-1 with argv[iarg]
      tmpstr.replace(0,arglength,argv[iarg],0,arglength);
      if(tmpstr=="-h")                  {RequestedHelp = true;}
      else if(tmpstr=="-input")         {InputProvided = true; inputFilename = argv[++iarg];}
      else if(tmpstr=="-centerlines")   {CenterlinesProvided = true; centerlinesFilename = argv[++iarg];}
      else if(tmpstr=="-output")        {OutputProvided = true; outputFilename = argv[++iarg];}
      else if(tmpstr=="-groupids")      {groupIdsArrayName = argv[++iarg];}
      else if(tmpstr=="-radius")        {radiusArrayName = argv[++iarg];}
      else if(tmpstr=="-blanking")      {blankingArrayName = argv[++iarg];}
      else if(tmpstr=="-cutoffradius")  {cutoffRadiusFactor = atof(argv[++iarg]);}
      else if(tmpstr=="-clipvalue")     {clipValue = atof(argv[++iarg]);}
      else if(tmpstr=="-useradiusinfo") {useRadiusInfo = atoi(argv[++iarg]);}
      else if(tmpstr=="-usevmtkclipping")               {useVmtkClipping = atoi(argv[++iarg]);}
      else if(tmpstr=="-enforceboundarydirections")     {enforceBoundaryDirections = atoi(argv[++iarg]);}
      else if(tmpstr=="-polycubedivisions")             {polycubeDivisions = atoi(argv[++iarg]);}
      else if(tmpstr=="-polycubeunitlength")            {polycubeUnitLength = atof(argv[++iarg]);}
      else if(tmpstr=="-normalsweighting")              {normalsWeighting = atof(argv[++iarg]);}
      else if(tmpstr=="-isvasculature")                 {isVasculature = atoi(argv[++iarg]);}
      else if(tmpstr=="-numberofcenterlineremovepts")   {numberOfCenterlineRemovePts = atoi(argv[++iarg]);}
      else if(tmpstr=="-boundaryenforcefactor")         {boundaryEnforceFactor = atoi(argv[++iarg]);}
      else if(tmpstr=="-writecenterlinegraph")          {writeCenterlineGraph = atoi(argv[++iarg]);}
      else if(tmpstr=="-writemergedcenterlines")        {writeMergedCenterlines = atoi(argv[++iarg]);}
      else if(tmpstr=="-writesurfacepolycube")          {writePolycubePd = atoi(argv[++iarg]);}
      else if(tmpstr=="-writevolumepolycube")           {writePolycubeUg = atoi(argv[++iarg]);}
      else if(tmpstr=="-writefinalhexmesh")             {writeFinalHexMesh = atoi(argv[++iarg]);}
      else if(tmpstr=="-writeall")                      {writeAll = atoi(argv[++iarg]);}
      else {cout << argv[iarg] << " is not a valid argument. Ask for help with -h." << endl; RequestedHelp = true; return EXIT_FAILURE;}
      // reset tmpstr for next argument
      tmpstr.erase(0,arglength);
  }

  if (RequestedHelp || !InputProvided || !CenterlinesProvided)
  {
    cout << endl;
    cout << "usage:" <<endl;
    cout << "  GroupsSegmenter -input [Input Filename] -centerlines [Centerlines] -output [Output Filename] -groupids [GroupIds Array Name] ..." << endl;
    cout << endl;
    cout << "COMMAND-LINE ARGUMENT SUMMARY" << endl;
    cout << "  -h                             : Display usage and command-line argument summary"<< endl;
    cout << "  -input                         : Input file name (.vtp or .stl)"<< endl;
    cout << "  -centerlines                   : Centerlines file name (.vtp)"<< endl;
    cout << "  -output                        : Output file name"<< endl;
    cout << "  -groupids                      : Name to be used for group ids [default GroupIds]"<< endl;
    cout << "  -radius                        : Name on centerlines describing maximum inscribed sphere radius [default MaximumInscribedSphereRadius]"<< endl;
    cout << "  -blanking                      : Name on centerlines describing whether line is part of bifurcation region or not [default Blanking]"<< endl;
    cout << "  -cutoffradius                  : Value at which a certain distance away from the centerline the function that is used for clipping is set to a large value, essentially clipping everything outside that radius [default 1.0e32]"<< endl;
    cout << "  -clipvalue                     : Value to use for clipping function [default 0.0]"<< endl;
    cout << "  -useradiusinfo                 : Use radius to help in clipping operation [default 1]"<< endl;
    cout << "  -usevmtkclipping               : Use the original vmtk clipping algorithm which can use the full centerlines rather than the modified centerline clipping [default 0]" << endl;
    cout << "  -enforceboundarydirections     : At separating patches, enforce the boundary directions by modifying the clustering vectors [default 1]" << endl;
    cout << "  -polycubedivisions             : The number of divisions for the width and height of the polycube [default 10]" << endl;
    cout << "  -polycubeunitlength            : The unit length for each division of the polycube. If 0.0, an approximate size will be found using the average radius of the model [default 0.0]" << endl;
    cout << "  -normalsweighting              : For the individual branch clustering, the weighting to put on normals. Should vary between 0 and 1. 1.0 will cluster only based on surface normals. 0.0 will cluster only based on position around the centerline [default 0.8]" << endl;
    cout << "  -isvasculature                 : Flag to indicate whether model is a vascular model with truncated boundaries. If model is not vasculature, the ends of the centerlines must be removed and the ends of the vessels need to be clustered based on position [default 1]" << endl;
    cout << "  -numberofcenterlineremovepts   : Number of centerline points to remove from the end of the branches if the model is not vasculature [default 3]" << endl;
    cout << "  -boundaryenforcefactor         : Approximately represents the number of centerline points to enforce per branch. Typically a fairly low integer works well. The larger the value, the larger the portion of the vessel is set explicitly, and sometimes this can cause large problems. [default 1]" << endl;
    cout << "  -writecenterlinegraph          : Write the centerline graph to file [default 0]" << endl;
    cout << "  -writemergedcenterlines        : Write the merged centerlines to file [default 0]" << endl;
    cout << "  -writesurfacepolycube          : Write the surface polycube to file [default 0]" << endl;
    cout << "  -writevolumepolycube           : Write the volume polycube to file [default 0]" << endl;
    cout << "  -writefinalhexmesh             : Write the final hex mesh to file [default 0]" << endl;
    cout << "  -writeall                      : Write everything [default 0]" << endl;
    cout << "END COMMAND-LINE ARGUMENT SUMMARY" << endl;
    return EXIT_FAILURE;
  }
  if (!OutputProvided)
  {
    cout << "WARNING: Output Filename not provided, setting output name based on the input filename" <<endl;
    std::string newDirName = vtkSVIOUtils::GetPath(inputFilename)+"/"+vtkSVIOUtils::GetRawName(inputFilename);
    // Only mac and linux!!!
    system(("mkdir -p "+newDirName).c_str());
    outputFilename = vtkSVIOUtils::GetPath(inputFilename)+"/"+vtkSVIOUtils::GetRawName(inputFilename)+"/"+vtkSVIOUtils::GetRawName(inputFilename)+"_Segmented.vtp";
  }
  if (writeAll)
  {
    writeCenterlineGraph = 1;
    writeMergedCenterlines = 1;
    writePolycubePd = 1;
    writePolycubeUg = 1;
    writeFinalHexMesh = 1;
  }

  // Call Function to Read File
  std::cout<<"Reading Files..."<<endl;
  vtkNew(vtkPolyData, inputPd);
  if (vtkSVIOUtils::ReadInputFile(inputFilename,inputPd) != 1)
    return EXIT_FAILURE;
  vtkNew(vtkPolyData, centerlinesPd);
  if (vtkSVIOUtils::ReadInputFile(centerlinesFilename,centerlinesPd) != 1)
    return EXIT_FAILURE;

  // Filter
  vtkNew(vtkSVGroupsSegmenter, Grouper);

  //OPERATION
  std::cout<<"Performing Operation..."<<endl;
  Grouper->SetInputData(inputPd);
  Grouper->SetCenterlines(centerlinesPd);
  Grouper->SetCenterlineGroupIdsArrayName(groupIdsArrayName.c_str());
  Grouper->SetGroupIdsArrayName(groupIdsArrayName.c_str());
  Grouper->SetCenterlineRadiusArrayName(radiusArrayName.c_str());
  Grouper->SetBlankingArrayName(blankingArrayName.c_str());
  Grouper->SetCutoffRadiusFactor(cutoffRadiusFactor);
  Grouper->SetClipValue(clipValue);
  Grouper->ClipAllCenterlineGroupIdsOn();
  Grouper->SetUseRadiusInformation(useRadiusInfo);
  Grouper->SetUseVmtkClipping(useVmtkClipping);
  Grouper->SetEnforceBoundaryDirections(enforceBoundaryDirections);
  Grouper->SetPolycubeDivisions(polycubeDivisions);
  Grouper->SetPolycubeUnitLength(polycubeUnitLength);
  Grouper->SetNormalsWeighting(normalsWeighting);
  Grouper->SetIsVasculature(isVasculature);
  Grouper->SetNumberOfCenterlineRemovePts(numberOfCenterlineRemovePts);
  Grouper->SetBoundaryEnforceFactor(boundaryEnforceFactor);
  Grouper->Update();
  std::cout<<"Done"<<endl;

  //Write Files
  std::cout<<"Writing Files..."<<endl;
  vtkSVIOUtils::WriteVTPFile(outputFilename, Grouper->GetOutput(0));
  if (writeCenterlineGraph)
    vtkSVIOUtils::WriteVTPFile(outputFilename, Grouper->GetGraphPd(), "_CenterlineGraph");
  if (writeMergedCenterlines)
    vtkSVIOUtils::WriteVTPFile(outputFilename, Grouper->GetMergedCenterlines(), "_MergedCenterlines");
  if (writePolycubePd)
    vtkSVIOUtils::WriteVTPFile(outputFilename, Grouper->GetPolycubePd(), "_PolycubePd");
  if (writePolycubeUg)
    vtkSVIOUtils::WriteVTUFile(outputFilename, Grouper->GetPolycubeUg(), "_PolycubeUg");
  if (writeFinalHexMesh)
    vtkSVIOUtils::WriteVTUFile(outputFilename, Grouper->GetFinalHexMesh(), "_FinalHexMesh");


  //Exit the program without errors
  return EXIT_SUCCESS;
}
