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
// This file contains the processing for the executable option "stats"
// which generates some statistics for SAM/BAM files.

#ifndef __INDEL_DISCORDANCE_H__
#define __INDEL_DISCORDANCE_H__

#include "BamExecutable.h"
#include "PileupElement.h"
#include "SimpleStats.h"

class IndelDiscordance : public BamExecutable
{
public:
    IndelDiscordance();

    static void indelDiscordanceDescription();
    void description();
    void usage();
    int execute(int argc, char **argv);

private:
    static const String DEFAULT_UM_REF_LOC;
    // 0-based start of non-pseudo-autosomal
    static const int DEFAULT_START_POS = 2699520;
    // 0-based exclusive end of non-pseudo-autosomal
    static const int DEFAULT_END_POS = 154931043;
    static const int DEFAULT_MIN_DEPTH = 2;
    static const int DEFAULT_MIN_REPEAT = 1;
    static const int DEFAULT_SUM_REPEAT = 5;
    // Default multiplier to the average depth to get the
    // maximum depth to include.
    static const int DEFAULT_AVG_DEPTH_MULTIPLIER = 3;

    class PileupElementIndelDiscordance : public PileupElement
    {
    public:
        struct DepthInfo
        {
            // Number of sites with this depth checked for deletion/insertion discordance.
            uint32_t count;
            // Number of discordant sites with this depth due to deletion/insertion.
            uint32_t discordantCount;
        };

        struct RepeatInfoAvgs
        {
            // Accumulate the Average Discordant Lengths of
            // an insertion/deletion.
            RunningStat avgDisLens;
            // Accumulate the Average Lengths of an insertion/deletion.
            RunningStat avgLens;
            // used to calc avg depth stat.
            // Number of reads for each position 
            //checked for insertion/deletion.
            RunningStat runningDepth;

        };

        struct RepeatInfo
        {
            RepeatInfoAvgs delAvgs;
            RepeatInfoAvgs insAvgs;
            // Depth information for this repeat.
            std::map<uint32_t, DepthInfo> delDepthInfo;
            std::map<uint32_t, DepthInfo> insDepthInfo;
        };

        // Structure containing the average number of reads
        // that were checked for deletion at a position.
        static RunningStat ourRunningDelCheckDepthStat;

        // Structure containing the average number of reads
        // that were checked for insertion at a position.
        // The last position of a read is not checked for an
        // insertion because there is no trailing aligned base
        // so this is different than the number of reads
        // checked for deletion.
        static RunningStat ourRunningInsCheckDepthStat;

        // Map indexed by repeat length that gives the repeat info
        static std::map<uint32_t, RepeatInfo> ourRepeatInfo;

        /// Set reference.
        static void setReference(GenomeSequence& reference)
        {
            ourReference = &reference;
        }

        static void setMinDepth(int minDepth) {ourMinDepth = minDepth;}
        static void setPrintPos(bool printPos) {ourPrintPos = printPos;}

        PileupElementIndelDiscordance();
        
        virtual ~PileupElementIndelDiscordance();
        
        // Add an entry to this pileup element.  
        virtual void addEntry(SamRecord& record);
        
        // Perform the analysis associated with this class.
        virtual void analyze();
        
        // Resets the entry, setting the new position associated with this
        // element.
        virtual void reset(int32_t refPosition);
        
    private:

        PileupElementIndelDiscordance(const PileupElement& q);
        
        void initVars();
        void releaseRecords();

        static int ourMinDepth;
        static bool ourPrintPos;
        int myNumDeletion;
        int myNumMatch;
        int myNumInsertion;
        int myNumNoInsertion;
        RunningStat myInsertLen;
        RunningStat myDeletionLen;

        static GenomeSequence* ourReference;
    };

    double calcErrorRate(std::map<uint32_t, PileupElementIndelDiscordance::DepthInfo>& depthInfo, double maxDepth);
};

#endif
