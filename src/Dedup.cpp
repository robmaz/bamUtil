/*
 *  Copyright (C) 2010-2012  Regents of the University of Michigan
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

#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <unistd.h>
#include <getopt.h>
#include "SamFile.h"
#include "Dedup.h"
#include "Logger.h"
#include "SamHelper.h"
#include "SamStatus.h"
#include "BgzfFileType.h"

const int Dedup::DEFAULT_MIN_QUAL = 15;
const uint32_t Dedup::CLIP_OFFSET = 1000;

Dedup::~Dedup()
{
    // clean up the maps.
    // First free any fragment records.
    for(FragmentMap::iterator iter = myFragmentMap.begin(); 
        iter != myFragmentMap.end(); iter++)
    {
        mySamPool.releaseRecord(iter->second.recordPtr);
    }
    myFragmentMap.clear();

    // Free any paired records.
    for(PairedMap::iterator iterator = myPairedMap.begin(); 
        iterator != myPairedMap.end(); iterator++)
    {
        // These are not duplicates, but we are done with them, so release them.
        mySamPool.releaseRecord(iterator->second.record1Ptr);
        mySamPool.releaseRecord(iterator->second.record2Ptr);
    }
    myPairedMap.clear();

    // Free any entries in the mate map.
    for(MateMap::iterator iter = myMateMap.begin(); 
        iter != myMateMap.end(); iter++)
    {
        mySamPool.releaseRecord(iter->second.recordPtr);
    }
    myMateMap.clear();
}

void Dedup::dedupDescription()
{
    std::cerr << " dedup - Mark Duplicates\n";
}


void Dedup::description()
{
    dedupDescription();
}


void Dedup::usage()
{
    std::cerr << "Usage: ./bam dedup --in <InputBamFile> --out <OutputBamFile> [--minQual <minPhred>] [--log <logFile>] [--oneChrom] [--rmDups] [--force] [--verbose] [--noeof] [--params] [--recab] ";
    myRecab.recabSpecificUsageLine();
    std::cerr << std::endl << std::endl;
    std::cerr << "Required parameters :" << std::endl;
    std::cerr << "\t--in <infile>   : input BAM file name (must be sorted)" << std::endl;
    std::cerr << "\t--out <outfile> : output BAM file name (same order with original file)" << std::endl;
    std::cerr << "Optional parameters : (see SAM format specification for details)" << std::endl;
    std::cerr << "\t--minQual <int> : only add scores over this phred quality when determining a read's quality (default: "
              << DEFAULT_MIN_QUAL << ")" << std::endl;
    std::cerr << "\t--log <logfile> : log and summary statistics (default: [outfile].log, or stderr if --out starts with '-')" << std::endl;
    std::cerr << "\t--oneChrom      : Treat reads with mates on different chromosomes as single-ended." << std::endl;
    std::cerr << "\t--rmDups        : Remove duplicates (default is to mark duplicates)" << std::endl;
    std::cerr << "\t--force         : Allow mark-duplicated BAM file and force unmarking the duplicates" << std::endl;
    std::cerr << "                    Default is to throw errors when trying to run a mark-duplicated BAM" << std::endl;
    std::cerr << "\t--verbose       : Turn on verbose mode" << std::endl;
    std::cerr << "\t--noeof         : do not expect an EOF block on a bam file." << std::endl;
    std::cerr << "\t--params        : print the parameter settings" << std::endl;
    std::cerr << "\t--recab         : Recalibrate in addition to deduping" << std::endl;
    myRecab.recabSpecificUsage();
    std::cerr << "\n" << std::endl;
}

int Dedup::execute(int argc, char** argv) 
{
    /* --------------------------------
     * process the arguments
     * -------------------------------*/
    String inFile, outFile, logFile;
    myDoRecab = false;
    bool removeFlag = false;
    bool verboseFlag = false;
    myForceFlag = false;
    myNumMissingMate = 0;
    myMinQual = DEFAULT_MIN_QUAL;
    bool noeof = false;
    bool params = false;
    LongParamContainer parameters;
    parameters.addGroup("Required Parameters");
    parameters.addString("in", &inFile);
    parameters.addString("out", &outFile);
    parameters.addGroup("Optional Parameters");
    parameters.addInt("minQual", & myMinQual);
    parameters.addString("log", &logFile);
    parameters.addBool("oneChrom", &myOneChrom);
    parameters.addBool("recab", &myDoRecab);
    parameters.addBool("rmDups", &removeFlag);
    parameters.addBool("force", &myForceFlag);
    parameters.addBool("verbose", &verboseFlag);
    parameters.addBool("noeof", &noeof);
    parameters.addBool("params", &params);
    myRecab.addRecabSpecificParameters(parameters);

    ParameterList inputParameters;
    inputParameters.Add(new LongParameters ("Input Parameters", 
                                            parameters.getLongParameterList()));
    
    inputParameters.Read(argc-1, &(argv[1]));
    
    // If no eof block is required for a bgzf file, set the bgzf file type to 
    // not look for it.
    if(noeof)
    {
        // Set that the eof block is not required.
        BgzfFileType::setRequireEofBlock(false);
    }

    if(inFile.IsEmpty())
    {
        usage();
        inputParameters.Status();
        std::cerr << "Specify an input file" << std::endl;
        abort();
    }

    if(outFile.IsEmpty())
    {
        usage();
        inputParameters.Status();
        std::cerr << "Specify an output file" << std::endl;
        abort();
    }

    if(logFile.IsEmpty())
    {
        logFile = outFile + ".log";
    }
    
    if(params)
    {
        inputParameters.Status();
    }
    
    Logger::gLogger = new Logger(logFile.c_str(), verboseFlag);

    /* -------------------------------------------------------------------
     * The arguments are processed.  Prepare the input BAM file,
     * instantiate dedup, and construct the read group library map
     * ------------------------------------------------------------------*/

    SamFile samIn;

    samIn.OpenForRead(inFile.c_str());
    // If the file isn't sorted it will throw an exception.
    samIn.setSortedValidation(SamFile::COORDINATE);

    SamFileHeader header;
    samIn.ReadHeader(header);

    buildReadGroupLibraryMap(header);

    lastReference = -1;
    lastCoordinate = -1;

    // for keeping some basic statistics
    uint32_t recordCount = 0;
    uint32_t pairedCount = 0;
    uint32_t properPairCount = 0;
    uint32_t unmappedCount = 0;
    uint32_t reverseCount = 0;
    uint32_t qualCheckFailCount = 0;

    // Now we start reading records
    SamRecord* recordPtr;
    SamStatus::Status returnStatus = SamStatus::SUCCESS;
    while(returnStatus == SamStatus::SUCCESS)
    {
        recordPtr = mySamPool.getRecord();
        if(recordPtr == NULL)
        {
            std::cerr << "Failed to allocate enough records\n";
            return(-1);
        }
        if(!samIn.ReadRecord(header, *recordPtr))
        {
            returnStatus = samIn.GetStatus();
            continue;
        }
        // Take note of properties of this record
        int flag = recordPtr->getFlag();
        if(SamFlag::isPaired(flag))     ++pairedCount;
        if(SamFlag::isProperPair(flag)) ++properPairCount;
        if(SamFlag::isReverse(flag))    ++reverseCount;
        if(SamFlag::isQCFailure(flag))  ++qualCheckFailCount;
        if(SamFlag::isDuplicate(flag) && !myForceFlag)
        {
            Logger::gLogger->error("There are records already duplicate marked.");
            Logger::gLogger->error("Use -f to clear the duplicate flag and start the deduping procedure over");
        }

        // put the record in the appropriate maps:
        //   single reads go in myFragmentMap
        //   paired reads go in myPairedMap
        recordCount = samIn.GetCurrentRecordCount();

        // if we have moved to a new position, look back at previous reads for duplicates
        if (hasPositionChanged(*recordPtr))
        {
            cleanupPriorReads(recordPtr);
        }

        // Deduping is only for mapped reads.
        if(!SamFlag::isMapped(flag))
        {
            ++unmappedCount;
            // Unmapped, nothing more to do with this record, so
            // release the pointer.
            mySamPool.releaseRecord(recordPtr);
        }
        else
        {
            checkDups(*recordPtr, recordCount);
        }
        // let the user know we're not napping
        if (verboseFlag && (recordCount % 100000 == 0))
        {
            Logger::gLogger->writeLog("recordCount=%u singleKeyMap=%u pairedKeyMap=%u, dictSize=%u", 
                                      recordCount, myFragmentMap.size(), 
                                      myPairedMap.size(), 
                                      myMateMap.size());
        }
    }

    // we're finished reading record so clean up the duplicate search and
    //  close the input file
    cleanupPriorReads(NULL);
    samIn.Close();

    // print some statistics
    Logger::gLogger->writeLog("--------------------------------------------------------------------------");
    Logger::gLogger->writeLog("SUMMARY STATISTICS OF THE READS");
    Logger::gLogger->writeLog("Total number of reads: %u",recordCount);
    Logger::gLogger->writeLog("Total number of paired-end reads: %u",
                              pairedCount);
    Logger::gLogger->writeLog("Total number of properly paired reads: %u",
                              properPairCount);
    Logger::gLogger->writeLog("Total number of unmapped reads : %u",
                              unmappedCount);
    Logger::gLogger->writeLog("Total number of reverse strand mapped reads: %u",
                              reverseCount);
    Logger::gLogger->writeLog("Total number of QC-failed reads : %u",
                              qualCheckFailCount);
    Logger::gLogger->writeLog("Size of singleKeyMap (must be zero) : %u",
                              myFragmentMap.size());
    Logger::gLogger->writeLog("Size of pairedKeyMap (must be zero) : %u",
                              myPairedMap.size());
    Logger::gLogger->writeLog("Total number of missing mates: %u",
                              myNumMissingMate);
    Logger::gLogger->writeLog("--------------------------------------------------------------------------");
    Logger::gLogger->writeLog("Sorting the indices of %d duplicated records",
                              myDupList.size());

    // sort the indices of duplicate records
    std::sort(myDupList.begin(), myDupList.end(),
              std::less<uint32_t> ());

    // get ready to write the output file by making a second pass
    // through the input file
    samIn.OpenForRead(inFile.c_str());
    samIn.ReadHeader(header);

    SamFile samOut;
    samOut.OpenForWrite(outFile.c_str());
    samOut.WriteHeader(header);

    // If we are recalibrating, output the model information.
    if(myDoRecab)
    {
        myRecab.modelFitPrediction(outFile);
    }

    // an iterator to run through the duplicate indices
    int currentDupIndex = 0;
    bool moreDups = !myDupList.empty();

    // let the user know what we're doing
    Logger::gLogger->writeLog("\nWriting %s", outFile.c_str());

    // count the duplicate records as a check
    uint32_t singleDuplicates(0), pairedDuplicates(0);

    // start reading records and writing them out
    SamRecord record;
    while(samIn.ReadRecord(header, record))
    {
        uint32_t currentIndex = samIn.GetCurrentRecordCount();

        bool foundDup = moreDups &&
            (currentIndex == myDupList[currentDupIndex]);

        // modify the duplicate flag and write out the record,
        // if it's appropriate
        int flag = record.getFlag();
        if (foundDup)
        {   
            // this record is a duplicate, so mark it.
            record.setFlag( flag | 0x400 );
            currentDupIndex++;
            // increment duplicate counters to verify we found them all
            if ( ( ( flag & 0x0001 ) == 0 ) || ( flag & 0x0008 ) )
            { // unpaired or mate unmapped
                singleDuplicates++;
            }
            else
            {
                pairedDuplicates++;
            }
            // recalibrate if necessary.
            if(myDoRecab)
            {
                myRecab.processReadApplyTable(record);
            }

            // write the record if we are not removing duplicates
            if (!removeFlag ) samOut.WriteRecord(header, record);
        }
        else
        {
            if(myForceFlag)
            { 
                // this is not a duplicate we've identified but we want to
                // remove any duplicate marking
                record.setFlag( flag & 0xfffffbff ); // unmark duplicate
            }
            // Not a duplicate, so recalibrate if necessary.
            if(myDoRecab)
            {
                myRecab.processReadApplyTable(record);
            }
            samOut.WriteRecord(header, record);
        }
	
        // Let the user know we're still here
        if (verboseFlag && (currentIndex % 100000 == 0)) {
            Logger::gLogger->writeLog("recordCount=%u", currentIndex);
        }
    }

    // We're done.  Close the files and print triumphant messages.
    samIn.Close();
    samOut.Close();

    Logger::gLogger->writeLog("Successfully %s %u unpaired and %u paired duplicate reads", 
                              removeFlag ? "removed" : "marked" ,
                              singleDuplicates,
                              pairedDuplicates/2);
    Logger::gLogger->writeLog("\nDedup complete!");
    return 0;
}

// Now that we've reached coordinate on chromosome reference, look back and
// clean up any previous positions from being tracked.
void Dedup::cleanupPriorReads(SamRecord* record)
{
    static DupKey emptyKey;
    static DupKey tempKey2;

    // Set where to stop cleaning out the structures.
    // Initialize to the end of the structures.
    FragmentMap::iterator fragmentFinish = myFragmentMap.end();
    PairedMap::iterator pairedFinish = myPairedMap.end();
    uint64_t mateStopPos = 0;

    // If a record was specified, stop before this record.
    if(record != NULL)
    {
        int32_t reference = record->getReferenceID();
        int32_t coordinate = record->get0BasedPosition();
        tempKey2.cleanupKey(reference, coordinate);
        fragmentFinish = myFragmentMap.lower_bound(tempKey2);
        // Now do the same thing with the paired reads
        PairedKey pairedKey(emptyKey, tempKey2);
        pairedFinish = myPairedMap.lower_bound(pairedKey);
        mateStopPos =
            SamHelper::combineChromPos(reference, 
                                       coordinate);
    }

    // For each key k < fragmentFinish, release the record since we are
    // done with that position and it is not a duplicate.
    for(FragmentMap::iterator iter = myFragmentMap.begin(); 
        iter != fragmentFinish; iter++)
    {
        // If it is not paired, we are done with this record.
        if(!iter->second.paired)
        {
            // Unpaired, non-duplicate, so perform any additional handling.
            handleNonDuplicate(iter->second.recordPtr);
        }
    }
    // Erase the entries from the map.
    if(fragmentFinish != myFragmentMap.begin())
    {
        myFragmentMap.erase(myFragmentMap.begin(), fragmentFinish);
    }

    // Now do the same thing with the paired reads
    for(PairedMap::iterator iter = myPairedMap.begin(); 
        iter != pairedFinish; iter++)
    {
        PairedData* pairedData = &(iter->second);
        // These are not duplicates, but we are done with them, 
        // so perform any additional handling.
        handleNonDuplicate(pairedData->record1Ptr);
        handleNonDuplicate(pairedData->record2Ptr);
    }
    // Erase the entries.
    if (pairedFinish != myPairedMap.begin())
    {
        myPairedMap.erase(myPairedMap.begin(), pairedFinish);
    }

    // Clean up the Mate map from any reads whose mates were not found.
    // Loop through the mate map and release records prior to this position.
    MateMap::iterator mateIter;
    for(mateIter = myMateMap.begin(); mateIter != myMateMap.end(); mateIter++)
    {
        // stop if a record was specified and we have gone past the mate
        // stop position.  If no record was specified, we want to clean
        // it all out.
        if((record != NULL) && (mateIter->first >= mateStopPos))
        {
            break;
        }
        // Passed the mate, but it was not found.
        handleMissingMate(mateIter->second.recordPtr);
    }
    // Erase the entries.
    if(mateIter != myMateMap.begin())
    {
        myMateMap.erase(myMateMap.begin(), mateIter);
    }
    return;
}


// determine whether the record's position is different from the previous record
bool Dedup::hasPositionChanged(SamRecord& record)
{
    if (lastReference != record.getReferenceID() || 
        lastCoordinate < record.get0BasedPosition())
    {
        if (lastReference != record.getReferenceID())
        {
            lastReference = record.getReferenceID();
            Logger::gLogger->writeLog("Reading ReferenceID %d\n", lastReference);
        }
        lastCoordinate = record.get0BasedPosition();
        return true;
    }
    return false;
}

// When a record is read, check if it is a duplicate or
// store for future checking.
void Dedup::checkDups(SamRecord& record, uint32_t recordCount)
{
    // Only inside this method if the record is mapped.

    // Get the key for this record.
    static DupKey key;
    static DupKey mateKey;
    key.updateKey(record, getLibraryID(record));

    int flag = record.getFlag(); 
    bool recordPaired = SamFlag::isPaired(flag) && SamFlag::isMateMapped(flag);
    int sumBaseQual = getBaseQuality(record);

    int32_t chromID = record.getReferenceID();
    int32_t mateChromID = record.getMateReferenceID();

    // If we are one-chrom and the mate is not on the same chromosome, 
    // mark it as not paired.
    if(myOneChrom && (chromID != mateChromID))
    {
        recordPaired = false;
    }
    
    // Look in the map to see if an entry for this key exists.
    FragmentMapInsertReturn ireturn = 
        myFragmentMap.insert(std::make_pair(key, ReadData()));

    ReadData* readData = &(ireturn.first->second);

    // Mark this record's data in the fragment record if this is the first
    // entry or if it is a duplicate and the old record is not paired and 
    // the new record is paired or the has a higher quality.
    if((ireturn.second == true) ||
       ((readData->paired == false) && 
        (recordPaired || (sumBaseQual > readData->sumBaseQual))))
    {
        // If there was a previous record, mark it duplicate and release
        // the old record
        if(ireturn.second == false)
        {
            // Mark the old record as a DUPLICATE!
            myDupList.push_back(readData->recordIndex);
            mySamPool.releaseRecord(readData->recordPtr);
        }
        // Store this record for later duplicate checking.
        readData->sumBaseQual = sumBaseQual;
        readData->recordIndex = recordCount;
        readData->paired = recordPaired;
        if(recordPaired)
        {
            readData->recordPtr = NULL;
        }
        else
        {
            readData->recordPtr = &record;
        }
    }
    else
    {
        // The old record is not a duplicate so the new record is
        // a duplicate if it is not paired.
        if(recordPaired == false)
        {
            // This record is a duplicate, so mark it and release it.
            myDupList.push_back(recordCount);
            mySamPool.releaseRecord(&record);
        }
    }

    // Only paired processing is left, so return if not paired.
    if(recordPaired == false)
    {
        // Not paired, no more operations required, so return.
        return;
    }
    
    // This is a paired record, so check for its mate.
    uint64_t readPos = 
        SamHelper::combineChromPos(chromID,
                                   record.get0BasedPosition());
    uint64_t matePos =
        SamHelper::combineChromPos(mateChromID, 
                                   record.get0BasedMatePosition());
    SamRecord* mateRecord = NULL;
    int mateIndex = 0;
    
    // Check to see if the mate is prior to this record.
    if(matePos <= readPos)
    {
        // The mate map is stored by the mate position, so look for this 
        // record's position.
        // The mate should be in the mate map, so find it.
        std::pair<MateMap::iterator,MateMap::iterator> matches =
            myMateMap.equal_range(readPos);
        // Loop through the elements that matched the pos looking for the mate.
        for(MateMap::iterator iter = matches.first; 
            iter != matches.second; iter++)
        {
            if(strcmp((*iter).second.recordPtr->getReadName(), 
                      record.getReadName()) == 0)
            {
                // Found the match.
                ReadData* mateData = &((*iter).second);
                // Update the quality and track the mate record and index.
                sumBaseQual += mateData->sumBaseQual;
                mateIndex = mateData->recordIndex;
                mateRecord = mateData->recordPtr;
                // Remove the entry from the map.
                myMateMap.erase(iter);
                break;
            }
        }
    }
    if((mateRecord == NULL) && (matePos >= readPos))
    {
        // Haven't gotten to the mate yet, so store this record.
        MateMap::iterator mateIter = 
            myMateMap.insert(std::make_pair(matePos, ReadData()));
        mateIter->second.sumBaseQual = sumBaseQual;
        mateIter->second.recordPtr = &record;
        mateIter->second.recordIndex = recordCount;
        // No more processing for this record is necessary.
        return;
    }

    if(mateRecord == NULL)
    {
        // Passed the mate, but it was not found.
        handleMissingMate(&record);
        return;
    }

    // Make the paired key.
    mateKey.updateKey(*mateRecord, getLibraryID(*mateRecord));
    PairedKey pkey(key, mateKey);

    // Check to see if this pair is a duplicate.
    PairedMapInsertReturn pairedReturn = 
        myPairedMap.insert(std::make_pair(pkey,PairedData()));
    PairedData* storedPair = &(pairedReturn.first->second);

    // Check if we have already found a duplicate pair.
    // If there is no duplicate found, there is nothing more to do.
    if(pairedReturn.second == false)
    {
        // Duplicate found.
        bool keepStored = true;
        if(pairedReturn.first->second.sumBaseQual < sumBaseQual)
        {
            // The new pair has higher quality, so keep that.
            keepStored = false;
        }
        else if(pairedReturn.first->second.sumBaseQual == sumBaseQual)
        {
            // Same quality, so keep the one with the earlier record1Index.
            if(mateIndex < storedPair->record2Index)
            {
                // The new pair has an earlier first read, so keep that.
                keepStored = false;
            }
        }
        // Check to see which one should be kept by checking qualities.
        if(keepStored)
        {
            // The old pair had higher quality so mark the new pair as a
            // duplicate and release them.
            myDupList.push_back(mateIndex);
            myDupList.push_back(recordCount);
            mySamPool.releaseRecord(&record);
            mySamPool.releaseRecord(mateRecord);
        }
        else
        {
            // The new pair has higher quality, so keep that.
            // First mark the previous one as duplicates and release them.
            myDupList.push_back(storedPair->record1Index);
            myDupList.push_back(storedPair->record2Index);
            mySamPool.releaseRecord(storedPair->record1Ptr);
            mySamPool.releaseRecord(storedPair->record2Ptr);
            // Store this pair's information.
            storedPair->sumBaseQual = sumBaseQual;
            storedPair->record1Ptr = &record;
            storedPair->record2Ptr = mateRecord;
            storedPair->record1Index = recordCount;
            storedPair->record2Index = mateIndex;
        }
    }
    else
    {
        // Store this pair's information.
        storedPair->sumBaseQual = sumBaseQual;
        storedPair->record1Ptr = &record;
        storedPair->record2Ptr = mateRecord;
        storedPair->record1Index = recordCount;
        storedPair->record2Index = mateIndex;
    }
}


// Finds the total base quality of a read 
int Dedup::getBaseQuality(SamRecord & record) {
    const char* baseQualities = record.getQuality();
    int readLength = record.getReadLength();
    int quality = 0.;
    if(strcmp(baseQualities, "*") == 0)
    {
        return(0);
    }
    for(int i=0; i < readLength; ++i) {
        int q = static_cast<int>(baseQualities[i])-33;
        if ( q >= myMinQual ) quality += q;
    }
    return quality;
}


// build the read group library map
void Dedup::buildReadGroupLibraryMap(SamFileHeader& header) {
    rgidLibMap.clear();
    numLibraries = 0;
    std::map<std::string,uint32_t> libNameMap;
    
    SamHeaderRecord * headerRecord = header.getNextRGRecord();
    while(headerRecord != NULL) {
        std::string ID = headerRecord->getTagValue("ID");
        std::string LB = headerRecord->getTagValue("LB");

        if ( ID.empty() ) {
            std::string headerRecordString;
            headerRecord->appendString(headerRecordString);
            Logger::gLogger->error("Cannot find readGroup ID information in the header line %s", 
                                   headerRecordString.c_str());
        }
        if ( rgidLibMap.find(ID) != rgidLibMap.end() ) {
            Logger::gLogger->error("The readGroup ID %s is not a unique identifier",ID.c_str());
        }

        if ( LB.empty() ) {
            std::string headerRecordString;
            headerRecord->appendString(headerRecordString);
            Logger::gLogger->warning("Cannot find library information in the header line %s. Using empty string for library name",
                                     headerRecordString.c_str());
        }

        if ( libNameMap.find( LB ) != libNameMap.end() ) {
            rgidLibMap[ID] = libNameMap[LB];
        }
        else {
            numLibraries = libNameMap.size()+1;
            libNameMap[LB] = numLibraries;
            rgidLibMap[ID] = numLibraries;
        }
        headerRecord = header.getNextRGRecord();
    }

    if (numLibraries > 0xff) {
        Logger::gLogger->error("More than 255 library names are identified. Dedup currently only allows up to 255 library names");
    }
}    

// get the libraryID of a record
uint32_t Dedup::getLibraryID(SamRecord& record, bool checkTags) {
    if ( ( checkTags == false ) && ( numLibraries <= 1 ) ) {
        return 0; 
    } else {
        char tag[3];
        char vtype;
        void* value;
        std::string rgID;
        record.resetTagIter();
        while( record.getNextSamTag(tag,vtype,&value) != false ) {
            if ( ( tag[0] == 'R' ) && ( tag[1] == 'G' ) && ( vtype == 'Z' ) ) {
                if ( !rgID.empty() ) {
                    Logger::gLogger->error("Multiple RG tag found in one record. ReadName is %s",record.getReadName());
                }
                else if ( record.isStringType(vtype) ) {
                    String s = (String)*(String*)value;
                    rgID = s.c_str();
                }
                else {
                    Logger::gLogger->error("vtype is not string (Z) for RG tag");
                }
            }
        }
        if ( rgID.empty() ) {
            Logger::gLogger->error("No RG tag is found in read %s",record.getReadName());
            return 0;
        }
        else {
            std::map<std::string,uint32_t>::iterator it = rgidLibMap.find(rgID);
            if ( it != rgidLibMap.end() ) {
                return it->second;
            }
            else {
                Logger::gLogger->warning("RG tag %s does not exist in the header",rgID.c_str());
                return 0; // cannot be reached
            }
        }
    }
}


void Dedup::handleNonDuplicate(SamRecord* recordPtr)
{
    if(recordPtr == NULL)
    {
        return;
    }
    if(myDoRecab)
    {
        if(myForceFlag)
        { 
            // this is not a duplicate we've identified but we want to
            // remove any duplicate marking
            uint16_t flag = recordPtr->getFlag();
            if(SamFlag::isDuplicate(flag))
            {
                SamFlag::setNotDuplicate(flag);
                recordPtr->setFlag(flag); // unmark duplicate
            }
        }
        // Add to recalibration matrix.
        myRecab.processReadBuildTable(*recordPtr);
    }

    // Release the record.
    mySamPool.releaseRecord(recordPtr);
}


void Dedup::handleMissingMate(SamRecord* recordPtr)
{
    static bool firstDifferChrom = true;
    static bool firstSameChrom = true;

    if(recordPtr == NULL)
    {
        return;
    }

    // Passed the mate, but it was not found.
    if(recordPtr->getMateReferenceID() != recordPtr->getReferenceID())
    {
        if(firstDifferChrom)
        {
            std::cerr << "Mate on different chromosome was not found.\n"
                      << "If you are running single chromosome, consider "
                      << "using --oneChrom to treat reads with mates on "
                      << "different chromosomes as single-ended.\n";
            firstDifferChrom = false;
        }
    }
    else if(firstSameChrom)
    {
        std::cerr << "ERROR: Records with missing mate can't be checked for "
                  << "duplicates.\n";
        firstSameChrom = false;
    }

    // Don't consider this record to be a duplicate.
    // Release this record since there is nothing more to do with it.
    ++myNumMissingMate;
    handleNonDuplicate(recordPtr);
}
