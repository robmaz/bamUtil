/*
 *  Copyright (C) 2012  Regents of the University of Michigan
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

//////////////////////////////////////////////////////////////////////////
// This file contains the processing for the executable option "baseInfo"
// which generates some base information for SAM/BAM files.


#ifndef __BASE_INFO_H__
#define __BASE_INFO_H__

#include "StringArray.h"
#include "BamExecutable.h"
#include "SamFile.h"

class BaseInfo : public BamExecutable
{
public:
    BaseInfo();
    
    ~BaseInfo();
    
    static void baseInfoDescription();
    void description();
    void usage();
    int execute(int argc, char **argv);
    
private:
    void reset();

    bool getNextSection(SamFile& samIn);

    static const int DEFAULT_GAP_SIZE = 100;

    // Pointer to the region list file
    IFILE  myRegionList;

    int myStartPos;
    int myEndPos;

    String myRegBuffer;
    StringArray myRegColumn;
};


#endif
