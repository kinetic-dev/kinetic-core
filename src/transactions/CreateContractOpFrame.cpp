// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/CreateContractOpFrame.h"
#include "OfferExchange.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/OfferFrame.h"
#include "ledger/TrustFrame.h"
#include "util/Logging.h"
#include <algorithm>

#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

CreateContractOpFrame::CreateContractOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreateContract(mOperation.body.createContractOp())
{
}

bool
CreateContractOpFrame::doApply(Application& app, LedgerDelta& delta,
                              LedgerManager& ledgerManager)
{
    AccountFrame::pointer destAccount;

    Database& db = ledgerManager.getDatabase();

    destAccount =
        AccountFrame::loadAccount(delta, mCreateContract.destination, db);
    if (!destAccount)
    {
        if (mCreateContract.startingBalance < ledgerManager.getMinBalance(0))
        { // not over the minBalance to make an account
            app.getMetrics()
                .NewMeter({"op-create-contract", "failure", "low-reserve"},
                          "operation")
                .Mark();
            innerResult().code(CREATE_CONTRACT_LOW_RESERVE);
            return false;
        }
        else
        {
            int64_t minBalance =
                mSourceAccount->getMinimumBalance(ledgerManager);

            if ((mSourceAccount->getAccount().balance - minBalance) <
                mCreateContract.startingBalance)
            { // they don't have enough to send
                app.getMetrics()
                    .NewMeter({"op-create-contract", "failure", "underfunded"},
                              "operation")
                    .Mark();
                innerResult().code(CREATE_CONTRACT_UNDERFUNDED);
                return false;
            }

            auto ok =
                mSourceAccount->addBalance(-mCreateContract.startingBalance);
            assert(ok);

            mSourceAccount->storeChange(delta, db);

            destAccount = make_shared<AccountFrame>(mCreateContract.destination);
            destAccount->getAccount().seqNum =
                delta.getHeaderFrame().getStartingSequenceNumber();
            destAccount->getAccount().balance = mCreateContract.startingBalance;
            destAccount->getAccount().scriptHash.activate() = mCreateContract.scriptHash;

            destAccount->storeAdd(delta, db);

            app.getMetrics()
                .NewMeter({"op-create-contract", "success", "apply"},
                          "operation")
                .Mark();
            innerResult().code(CREATE_CONTRACT_SUCCESS);
            return true;
        }
    }
    else
    {
        app.getMetrics()
            .NewMeter({"op-create-contract", "failure", "already-exist"},
                      "operation")
            .Mark();
        innerResult().code(CREATE_CONTRACT_ALREADY_EXIST);
        return false;
    }
}

bool
CreateContractOpFrame::doCheckValid(Application& app)
{
    if (mCreateContract.startingBalance <= 0)
    {
        app.getMetrics()
            .NewMeter(
                {"op-create-contract", "invalid", "malformed-negative-balance"},
                "operation")
            .Mark();
        innerResult().code(CREATE_CONTRACT_MALFORMED);
        return false;
    }

    if (mCreateContract.destination == getSourceID())
    {
        app.getMetrics()
            .NewMeter({"op-create-contract", "invalid",
                       "malformed-destination-equals-source"},
                      "operation")
            .Mark();
        innerResult().code(CREATE_CONTRACT_MALFORMED);
        return false;
    }

    return true;
}
}
