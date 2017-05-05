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

/**
 * \class   vtkSVRawReader
 * \brief   read ASCII raw file
*/

#ifndef vtkSVRawReader_h
#define vtkSVRawReader_h

#include "vtkSVCommonModule.h" // For export macro
#include "vtkPolyDataAlgorithm.h"

class vtkCellArray;
class vtkFloatArray;
class vtkIncrementalPointLocator;
class vtkPoints;

class VTKSVCOMMON_EXPORT vtkSVRawReader : public vtkPolyDataAlgorithm
{
public:
  vtkTypeMacro(vtkSVRawReader,vtkPolyDataAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent);

  /**
   * Construct object with merging set to true.
   */
  static vtkSVRawReader *New();

  //@{
  /** \brief Set/Get FileName. */
  vtkSetStringMacro(FileName);
  vtkGetStringMacro(FileName);
  //@}

  //@{
  /**
   * Turn on/off merging of points/triangles.
   */
  vtkSetMacro(Merging,int);
  vtkGetMacro(Merging,int);
  vtkBooleanMacro(Merging,int);
  //@}

  //@{
  /**
   * Specify a spatial locator for merging points. By
   * default an instance of vtkMergePoints is used.
   */
  void SetLocator(vtkIncrementalPointLocator *locator);
  vtkGetObjectMacro(Locator,vtkIncrementalPointLocator);
  //@}

protected:
  vtkSVRawReader();
  ~vtkSVRawReader();

  /**
   * Create default locator. Used to create one when none is specified.
   */
  vtkIncrementalPointLocator* NewDefaultLocator();

  char *FileName;
  int Merging;
  vtkIncrementalPointLocator *Locator;

  int RequestData(vtkInformation *, vtkInformationVector **, vtkInformationVector *);
  int ReadRawFile(FILE *fp, vtkPoints*, vtkCellArray*);
private:
  vtkSVRawReader(const vtkSVRawReader&);
  void operator=(const vtkSVRawReader&);
};

#endif
