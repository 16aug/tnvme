/*
 * Copyright (c) 2011, Intel Corporation.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "writeDataPat_r10b.h"
#include "globals.h"
#include "createIOQContigPoll_r10b.h"
#include "createIOQDiscontigPoll_r10b.h"
#include "grpDefs.h"
#include "../Utils/kernelAPI.h"
#include "../Queues/ce.h"

namespace GrpBasicInit {


WriteDataPat_r10b::WriteDataPat_r10b(int fd, string grpName, string testName,
    ErrorRegs errRegs) :
    Test(fd, grpName, testName, SPECREV_10b, errRegs)
{
    // 66 chars allowed:     xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
    mTestDesc.SetCompliance("revision 1.0b, section 6");
    mTestDesc.SetShort(     "Write a well known data pattern to media");
    // No string size limit for the long description
    mTestDesc.SetLong(
        "Issue an NVM cmd set write command with a well known data pattern "
        "to namespace #1. The write command shall be completely generic.");
}


WriteDataPat_r10b::~WriteDataPat_r10b()
{
    ///////////////////////////////////////////////////////////////////////////
    // Allocations taken from the heap and not under the control of the
    // RsrcMngr need to be freed/deleted here.
    ///////////////////////////////////////////////////////////////////////////
}


WriteDataPat_r10b::
WriteDataPat_r10b(const WriteDataPat_r10b &other) : Test(other)
{
    ///////////////////////////////////////////////////////////////////////////
    // All pointers in this object must be NULL, never allow shallow or deep
    // copies, see Test::Clone() header comment.
    ///////////////////////////////////////////////////////////////////////////
}


WriteDataPat_r10b &
WriteDataPat_r10b::operator=(const WriteDataPat_r10b &other)
{
    ///////////////////////////////////////////////////////////////////////////
    // All pointers in this object must be NULL, never allow shallow or deep
    // copies, see Test::Clone() header comment.
    ///////////////////////////////////////////////////////////////////////////
    Test::operator=(other);
    return *this;
}


bool
WriteDataPat_r10b::RunCoreTest()
{
    /** \verbatim
     * Assumptions:
     * 1) All interrupts are disabled.
     * 2) Contigous IOQ pairs have been created by the RsrcMngr for group life
     * 3) The NVM cmd set is the active cmd set.
     * \endverbatim
     */

    WriteDataPattern();
    return true;
}


void
WriteDataPat_r10b::WriteDataPattern()
{
    uint64_t regVal;


    LOG_NRM("Calc buffer size to write %d logical blks to media",
        WRITE_DATA_PAT_NUM_BLKS);
    ConstSharedIdentifyPtr namSpcPtr = gInformative->GetIdentifyCmdNamspc(1);
    if (namSpcPtr == Identify::NullIdentifyPtr) {
        LOG_ERR("Namespace #1 must exist");
        throw exception();
    }
    uint64_t lbaDataSize = namSpcPtr->GetLBADataSize();


    LOG_NRM("Create data pattern to write to media");
    SharedMemBufferPtr dataPat = SharedMemBufferPtr(new MemBuffer());
    dataPat->Init(WRITE_DATA_PAT_NUM_BLKS * lbaDataSize);
    dataPat->SetDataPattern(MemBuffer::DATAPAT_INC_16BIT);
    dataPat->Dump(FileSystem::PrepLogFile(mGrpName, mTestName, "DataPat"),
        "Write buffer's data pattern");
    

    LOG_NRM("Create a generic write cmd to send data pattern to namspc 1");
    SharedWritePtr writeCmd = SharedWritePtr(new Write(mFd));
    send_64b_bitmask prpBitmask = (send_64b_bitmask)
        (MASK_PRP1_PAGE | MASK_PRP2_PAGE | MASK_PRP2_LIST);
    writeCmd->SetPrpBuffer(prpBitmask, dataPat);
    writeCmd->SetNSID(1);
    writeCmd->SetNLB(WRITE_DATA_PAT_NUM_BLKS-1);    // convert to 0-based value

    // Lookup objs which were created in a prior test within group
    SharedIOSQPtr iosqContig = CAST_TO_IOSQ(
        gRsrcMngr->GetObj(IOSQ_CONTIG_GROUP_ID))
    SharedIOCQPtr iocqContig = CAST_TO_IOCQ(
        gRsrcMngr->GetObj(IOCQ_CONTIG_GROUP_ID))
    SharedIOSQPtr iosqDiscontig = CAST_TO_IOSQ(
        gRsrcMngr->GetObj(IOSQ_DISCONTIG_GROUP_ID))
    SharedIOCQPtr iocqDiscontig = CAST_TO_IOCQ(
        gRsrcMngr->GetObj(IOCQ_DISCONTIG_GROUP_ID))

    LOG_NRM("Send the cmd to hdw via the contiguous IOQ's");
    SendToIOSQ(iosqContig, iocqContig, writeCmd, "contig");

    // To run the discontig part of this test, the hdw must support that feature
    if (gRegisters->Read(CTLSPC_CAP, regVal) == false) {
        LOG_ERR("Unable to determine Q memory requirements");
        throw exception();
    } else if (regVal & CAP_CQR) {
        LOG_NRM("Unable to utilize discontig Q's, DUT requires contig");
        return;
    }

    LOG_NRM("Send the cmd to hdw via the discontiguous IOQ's");
    SendToIOSQ(iosqDiscontig, iocqDiscontig, writeCmd, "discontig");
}


void
WriteDataPat_r10b::SendToIOSQ(SharedIOSQPtr iosq, SharedIOCQPtr iocq,
    SharedWritePtr writeCmd, string qualifier)
{
    uint16_t numCE;
    uint16_t ceRemain;
    uint16_t numReaped;
    uint32_t isrCount;


    LOG_NRM("Send the cmd to hdw via %s IOSQ", qualifier.c_str());
    iosq->Send(writeCmd);
    iosq->Dump(FileSystem::PrepLogFile(mGrpName, mTestName, "iosq", qualifier),
        "Just B4 ringing SQ doorbell, dump entire IOSQ contents");
    iosq->Ring();


    LOG_NRM("Wait for the CE to arrive in IOCQ");
    if (iocq->ReapInquiryWaitSpecify(DEFAULT_CMD_WAIT_ms, 1, numCE, isrCount)
        == false) {

        LOG_ERR("Unable to see completion of cmd");
        iocq->Dump(
            FileSystem::PrepLogFile(mGrpName, mTestName, "iocq", qualifier),
            "Unable to see any CE's in IOCQ, dump entire CQ contents");
        throw exception();
    } else if (numCE != 1) {
        LOG_ERR("The IOCQ should only have 1 CE as a result of a cmd");
        throw exception();
    }
    iocq->Dump(FileSystem::PrepLogFile(mGrpName, mTestName, "iocq", qualifier),
        "Just B4 reaping IOCQ, dump entire CQ contents");


    LOG_NRM("The CQ's metrics B4 reaping holds head_ptr needed");
    struct nvme_gen_cq iocqMetrics = iocq->GetQMetrics();
    KernelAPI::LogCQMetrics(iocqMetrics);

    LOG_NRM("Reaping CE from IOCQ, requires memory to hold reaped CE");
    SharedMemBufferPtr ceMemIOCQ = SharedMemBufferPtr(new MemBuffer());
    if ((numReaped = iocq->Reap(ceRemain, ceMemIOCQ, isrCount, numCE, true))
        != 1) {

        LOG_ERR("Verified there was 1 CE, but reaping produced %d", numReaped);
        throw exception();
    }
    LOG_NRM("The reaped CE is...");
    iocq->LogCE(iocqMetrics.head_ptr);

    union CE ce = iocq->PeekCE(iocqMetrics.head_ptr);
    ProcessCE::Validate(ce);  // throws upon error
}

}   // namespace
