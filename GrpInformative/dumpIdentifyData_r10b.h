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

#ifndef _DUMPIDENTIFYDATA_r10b_H_
#define _DUMPIDENTIFYDATA_r10b_H_

#include "test.h"
#include "../Queues/asq.h"
#include "../Queues/acq.h"


/** \verbatim
 * -----------------------------------------------------------------------------
 * ----------------Mandatory rules for children to follow-----------------------
 * -----------------------------------------------------------------------------
 * 1) See notes in the header file of the Test base class
 * \endverbatim
 */
class DumpIdentifyData_r10b : public Test
{
public:
    DumpIdentifyData_r10b(int fd, string grpName, string testName);
    virtual ~DumpIdentifyData_r10b();

    /**
     * IMPORTANT: Read Test::Clone() header comment.
     */
    virtual DumpIdentifyData_r10b *Clone() const
        { return new DumpIdentifyData_r10b(*this); }
    DumpIdentifyData_r10b &operator=(const DumpIdentifyData_r10b &other);
    DumpIdentifyData_r10b(const DumpIdentifyData_r10b &other);


protected:
    virtual bool RunCoreTest();


private:
    ///////////////////////////////////////////////////////////////////////////
    // Adding a member variable? Then edit the copy constructor and operator().
    ///////////////////////////////////////////////////////////////////////////
    void SendIdentifyCtrlrStruct(SharedASQPtr asq, SharedACQPtr acq);
    void SendIdentifyNamespaceStruct(SharedASQPtr asq, SharedACQPtr acq);
};


#endif