/******************************************************************************
   Copyright 2020 Embedded Office GmbH & Co. KG

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
******************************************************************************/

/******************************************************************************
* INCLUDES
******************************************************************************/

#include "co_core.h"

/******************************************************************************
* PRIVATE DEFINES
******************************************************************************/

#define COT_ENTRY_SIZE    (uint32_t)4
#define COT_OBJECT        (uint16_t)0x1016
#define COT_HB_COBID      (uint32_t)0x00000700

/******************************************************************************
* PRIVATE FUNCTIONS
******************************************************************************/

/* type functions */
static uint32_t COTNmtHbConsSize (struct CO_OBJ_T *, struct CO_NODE_T *, uint32_t);
static CO_ERR   COTNmtHbConsRead (struct CO_OBJ_T *, struct CO_NODE_T *, void*, uint32_t);
static CO_ERR   COTNmtHbConsWrite(struct CO_OBJ_T *, struct CO_NODE_T *, void*, uint32_t);
static CO_ERR   COTNmtHbConsInit (struct CO_OBJ_T *obj, struct CO_NODE_T *node);

/* helper functions */
static CO_ERR   CONmtHbConsActivate(CO_NMT *nmt, CO_HBCONS *hbc, uint16_t time, uint8_t nodeid);
static void     CONmtHbConsMonitor(void *parg);

/******************************************************************************
* PUBLIC GLOBALS
******************************************************************************/

const CO_OBJ_TYPE COTNmtHbCons = { COTNmtHbConsSize, COTNmtHbConsInit, COTNmtHbConsRead, COTNmtHbConsWrite };

/******************************************************************************
* PRIVATE TYPE FUNCTIONS
******************************************************************************/

static uint32_t COTNmtHbConsSize(struct CO_OBJ_T *obj, struct CO_NODE_T *node, uint32_t width)
{
    uint32_t result = (uint32_t)0;

    UNUSED(node);
    UNUSED(width);

    /* check for valid reference */
    if ((obj->Data) != (CO_DATA)0) {
        result = COT_ENTRY_SIZE;
    }
    return (result);
}

static CO_ERR COTNmtHbConsRead(struct CO_OBJ_T *obj, struct CO_NODE_T *node, void *buf, uint32_t len)
{
    CO_ERR     result = CO_ERR_NONE;
    CO_HBCONS *hbc;
    uint32_t   value;

    UNUSED(node);
    ASSERT_PTR_ERR(obj->Data, CO_ERR_BAD_ARG);

    hbc    = (CO_HBCONS *)(obj->Data);
    value  = (uint32_t)(hbc->Time);
    value |= ((uint32_t)(hbc->NodeId)) << 16;
    if (len == COT_ENTRY_SIZE) {
        *((uint32_t *)buf) = value;
    }
    return (result);
}

static CO_ERR COTNmtHbConsWrite(struct CO_OBJ_T *obj, struct CO_NODE_T *node, void *buf, uint32_t len)
{
    CO_ERR      result = CO_ERR_TYPE_WR;
    CO_HBCONS  *hbc;
    uint32_t    value  = 0;
    uint16_t    time;
    uint8_t     nodeid;

    ASSERT_PTR_ERR(obj->Data, CO_ERR_BAD_ARG);
    ASSERT_EQU_ERR(len, COT_ENTRY_SIZE, CO_ERR_BAD_ARG);

    hbc    = (CO_HBCONS *)(obj->Data);
    value  = *((uint32_t *)buf);
    time   = (uint16_t)value;
    nodeid = (uint8_t)(value >> 16);
    result = CONmtHbConsActivate(&node->Nmt, hbc, time, nodeid);
    return (result);
}

static CO_ERR COTNmtHbConsInit(struct CO_OBJ_T *obj, struct CO_NODE_T *node)
{
    CO_ERR     err;
    CO_HBCONS *hbc;
    CO_NMT    *nmt;
    uint8_t    num;

    ASSERT_PTR_ERR(obj,        CO_ERR_BAD_ARG);
    ASSERT_PTR_ERR(node,       CO_ERR_BAD_ARG);
    ASSERT_PTR_ERR(&node->Nmt, CO_ERR_BAD_ARG);

    /* check for heartbeat consumer object */
    if (CO_DEV(COT_OBJECT, 0) == CO_GET_DEV(obj->Key)) {
        /* clear consumer linked list */
        nmt = &node->Nmt;

        /* get number of consumers */
        (void)COObjRdValue(obj, node, &num, sizeof(num));
        while (num > 0) {
            /* get consumer subindex */
            obj = CODictFind(&node->Dict, CO_DEV(COT_OBJECT, num));

            /* check object entry */
            if (obj == 0) {
                return(CO_ERR_TYPE_INIT);
            }
            hbc = (CO_HBCONS *)(obj->Data);
            if (hbc == 0) {
                return(CO_ERR_TYPE_INIT);
            }

            /* activate consumer */
            err = CONmtHbConsActivate(nmt, hbc, hbc->Time, hbc->NodeId);
            if (err != CO_ERR_NONE) {
                node->Error = err;
            }

            num--;
        }
    }
    return (CO_ERR_NONE);
}

/******************************************************************************
* PRIVATE HELPER FUNCTIONS
******************************************************************************/

static CO_ERR CONmtHbConsActivate(CO_NMT *nmt, CO_HBCONS *hbc, uint16_t time, uint8_t nodeid)
{
    CO_ERR      result  = CO_ERR_NONE;
    int16_t     err;
    CO_HBCONS  *act;
    CO_HBCONS  *prev;
    CO_HBCONS  *found = 0;

    prev = 0;
    act  = nmt->HbCons;
    while (act != 0) {
        if (act->NodeId == nodeid) {
            found = act;
            break;
        }
        prev = act;
        act  = act->Next;
    }

    if (found != 0) {
        if (time > 0) {
            result = CO_ERR_OBJ_INCOMPATIBLE;
        } else {
            if (hbc->Tmr >= 0) {
                err = COTmrDelete(&nmt->Node->Tmr, hbc->Tmr);
                if (err < 0) {
                    result = CO_ERR_TMR_DELETE;
                }
            }
            hbc->Time   = time;
            hbc->NodeId = nodeid;
            hbc->Tmr    = -1;
            hbc->Event  = 0;
            hbc->State  = CO_INVALID;
            hbc->Node   = nmt->Node;
            if (prev == 0) {
                nmt->HbCons = hbc->Next;
            } else {
                prev->Next  = hbc->Next;
            }
            hbc->Next   = 0;
        }
    } else {
        hbc->Time   = time;
        hbc->NodeId = nodeid;
        hbc->Tmr    = -1;
        hbc->Event  = 0;
        hbc->State  = CO_INVALID;
        hbc->Node   = nmt->Node;

        if (time > 0) {
            hbc->Next   = nmt->HbCons;
            nmt->HbCons = hbc;
        } else {
            hbc->Next   = 0;
        }
    }

    return (result);
}

static void CONmtHbConsMonitor(void *parg)
{
    CO_NODE   *node;
    CO_HBCONS *hbc;
    CO_TMR    *tmr;
    uint32_t   ticks;

    hbc   = (CO_HBCONS *)parg;
    node  = hbc->Node;
    tmr   = &node->Tmr;
    ticks = COTmrGetTicks(tmr, hbc->Time, CO_TMR_UNIT_1MS);
    hbc->Tmr = COTmrCreate(tmr,
        ticks,
        0,
        CONmtHbConsMonitor,
        hbc);
    if (hbc->Tmr < 0) {
        node->Error = CO_ERR_TMR_CREATE;
    }
    if (hbc->Event < 0xFFu) {
        hbc->Event++;
    }
    CONmtHbConsEvent(&node->Nmt, hbc->NodeId);
}

/******************************************************************************
* PROTECTED API FUNCTION
******************************************************************************/

int16_t CONmtHbConsCheck(CO_NMT *nmt, CO_IF_FRM *frm)
{
    CO_HBCONS *hbc;
    CO_TMR    *tmr;
    CO_MODE    state;
    int16_t    result = -1;
    uint32_t   cobid;
    uint32_t   ticks;
    uint8_t    nodeid;

    cobid  = frm->Identifier;
    hbc    = nmt->HbCons;
    if (hbc == 0) {
        return (result);
    }
    if ((cobid >= COT_HB_COBID) &&
        (cobid <= COT_HB_COBID + 127)) {
        nodeid = (uint8_t)(cobid - COT_HB_COBID);
    } else {
        return (result);
    }
    tmr = &nmt->Node->Tmr;
    while (hbc != 0) {
        if (hbc->NodeId != nodeid) {
            hbc = hbc->Next;
        } else {
            if (hbc->Tmr >= 0) {
                COTmrDelete(tmr, hbc->Tmr);
            }
            ticks = COTmrGetTicks(tmr, hbc->Time, CO_TMR_UNIT_1MS);
            hbc->Tmr = COTmrCreate(tmr,
                ticks,
                0,
                CONmtHbConsMonitor,
                hbc);
            if (hbc->Tmr < 0) {
                nmt->Node->Error = CO_ERR_TMR_CREATE;
            }
            state = CONmtModeDecode(frm->Data[0]);
            if (hbc->State != state) {
                CONmtHbConsChange(nmt, hbc->NodeId, state);
            }
            hbc->State = state;
            result     = (int16_t)hbc->NodeId;
            break;
        }
    }

    return (result);
}

/******************************************************************************
* PUBLIC API FUNCTION
******************************************************************************/

int16_t CONmtGetHbEvents(CO_NMT *nmt, uint8_t nodeId)
{
    int16_t    result = -1;
    CO_HBCONS *hbc;

    if (nmt == 0) {
        CONodeFatalError();
        return (result);
    }

    hbc = nmt->HbCons;
    while (hbc != 0) {
        if (nodeId == hbc->NodeId) {
            result     = (int16_t)hbc->Event;
            hbc->Event = 0;
        }
        hbc = hbc->Next;
    }

    return (result);
}

CO_MODE CONmtLastHbState(CO_NMT *nmt, uint8_t nodeId)
{
    CO_MODE     result = CO_INVALID;
    CO_HBCONS  *hbc;

    if (nmt == 0) {
        CONodeFatalError();
        return (result);
    }

    hbc = nmt->HbCons;
    while (hbc != 0) {
        if (nodeId == hbc->NodeId) {
            result = hbc->State;
        }
        hbc = hbc->Next;
    }

    return (result);
}
