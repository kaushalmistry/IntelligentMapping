#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <queue>
#include <algorithm>
#include <functional>
#include <numeric>
#include <string>
#include <map>

using namespace std;

#define HIT 1
#define MISS 0
#define READ 0
#define WRITE 1
#define PageHitDelay 50
#define PageMissDelay 600
// In cycles
#define readFwQueueTransferDelay 2
#define readBwQueueTransferDelay 8
#define writeFwQueueTransferDelay 8
#define writeBwQueueTransferDelay 2

#define numOfChannel 2
#define numOfRankInEachChannel 2
#define numOfBankInEachRank 2
#define numOfPartsInEachBank 4
#define numRows 16
#define numCols 256

#define channelBits 1
#define rankBits 1
#define bankBits 1
#define rowBits 4
#define colBits 8

#define bankQSize 32

int maxAddr = 1 << 15;
long long clk = 0;

long long maxRankQSize = 0;
long long maxChannelQSize = 0;

/**
 * Request structure to hold the type of the request and the slicing of the request address.
*/
struct request {
    int address;
    int RW; // 0 - Read, 1 - Write
    int channel; // Channel bits
    int rank; // Rank bits
    int bank; // Bank Bits
    int part; // The part - Not part of address bits (calculated using row)
    int row; // Row address bits
    int col; // col address bits
    int arrivalTime;
    int departureTime;
    int rClk;
    int localAT;

    request() : RW(0), channel(0) {}
};



ofstream MTF("DRAM_Queue_Simulation.txt");

ofstream DATA_COLLECTION("QueueSize.csv");


// queue<request*> q;
/*
Queue will store the address and the clock ticking
When the request arrived and departed from the different queue.
1st element in the pair is the address
2nd element in the pair is another pair
    -> containing the arrival time and departure time of the request
*/


void moveRequestBw(queue<request*>& sourceQueue, queue<request*>& targetQueue, int delay) {
    if (!sourceQueue.empty() && (clk - sourceQueue.front()->localAT) >= delay) {
        // cout << "Pushing to backword queue @: " << clk << endl;
        request* r = sourceQueue.front();
        sourceQueue.pop();
        r->localAT = clk;
        targetQueue.push(r);
    }
}


class Bank {
	int Address;
	int RowBufferAddess;
	int PageSize, NumRowPerBank, NumColumnPerRow;
	int PageHit, PageMiss;
	int ReadCtr, WriteCtr;
    // Request counter to count number of requests in the bank
    long long requestCnt;

    int noOfRows, noOfCols;

	public:

    queue<request*> readForwardQueue;
    queue<request*> writeForwardQueue;
    queue<request*> readBackwardQueue;
    queue<request*> writeBackwardQueue;

    // Current request that is being processed by the bank
    vector<request*> processing;
    // Flag indicating whether new request can be processed or not
    bool canProcess; // True - cam, false - can't
    int curReadQSize;
    int curWriteQSize;


	Bank() {}

	void init(int r, int c) {
        noOfRows = r;
        noOfCols = c;
		NumRowPerBank = r;
		NumColumnPerRow = c;
		PageSize = NumColumnPerRow;
        canProcess = true;
        curReadQSize = 0;
        curWriteQSize = 0;
        requestCnt = 0;
	}

    // Accessing the specific row of the bank
	int Access(request* addr) {

		if (addr->RW) WriteCtr++; 
        else ReadCtr++;

		if (RowBufferAddess == addr->row) {
			PageHit++;
            return PageHitDelay;
		}
		PageMiss++;
        RowBufferAddess = addr->row;
		return PageMissDelay;
	}

    void processRequest() {
        if (canProcess) return;
        processing[0]->rClk--;
        if (processing[0]->rClk == 0) {
            // cout << "Processed: " << clk << endl;
            canProcess = true;
            if (processing[0]->RW) {
                processing[0]->localAT = clk;
                writeBackwardQueue.push(processing[0]);
            } else {
                processing[0]->localAT = clk;
                readBackwardQueue.push(processing[0]);
            }
            processing.pop_back();
        }
    }

    void updateRequests(queue<request*>& sourceQueue, int delay) {
        if (canProcess && !sourceQueue.empty() && (clk - sourceQueue.front()->localAT) >= delay) {
            // cout << "Putting it to process @: " << clk << endl;
            // cout << "started processing: " << clk << endl;
            request* r = sourceQueue.front();
            sourceQueue.pop();
            r->localAT = clk;
            r->rClk = Access(r);
            processing.push_back(r);
            canProcess = false;
            // Updating request count at bank
            requestCnt++;
            // if (r->RW) curWriteQSize--;
            // else curReadQSize--;
        }
    }


    /**
     * Reads the forward queues and address the request.
     * As of now reading both the read and write queue
     * But only one can be done.
    */
    void updateQueues() {
        processRequest();
        // Reading the read-forward-queue
        updateRequests(readForwardQueue, readFwQueueTransferDelay);
        updateRequests(writeForwardQueue, writeFwQueueTransferDelay);
    }

    int getNumPageHits() { return PageHit; }
	int getNumPageMisses() { return PageMiss; }
	int getNumWrite() { return WriteCtr; }
	int getNumRead() { return ReadCtr; }

    long long getNumRequest() { return requestCnt; }
};



class Rank {
    int noOfBanks = 0, noOfRows = 0, noOfCols = 0;

    public:
    Bank* banks;
    queue<request*> readForwardQueue;
    queue<request*> writeForwardQueue;
    queue<request*> readBackwardQueue;
    queue<request*> writeBackwardQueue;

    Rank() {}

    void init(int b, int rows, int cols) {
        noOfBanks = b;
        noOfRows = rows;
        noOfCols = cols;

        banks = new Bank[noOfBanks];

        for (int i = 0; i < noOfBanks; i++) {
            banks[i].init(noOfRows, noOfCols);
        }
    }

    void moveRequestsFw(queue<request*>& source, int delay) {
        // Keep track of the queue size
        if (source.size() > maxRankQSize) maxRankQSize = source.size();

        if (!source.empty() && (clk - source.front()->localAT) >= delay) {
            // cout << "Pushing from rank to bank queue @: " << clk << endl;
            // Check if the bank queue has space to accomodate new request
            request* r = source.front();

            if (r->RW) {
                banks[r->bank].curWriteQSize++;
                banks[r->bank].writeForwardQueue.push(r);
            } else {
                banks[r->bank].curReadQSize++;
                banks[r->bank].readForwardQueue.push(r);
            }

            r->localAT = clk;
            source.pop();
        }
    }

    /**
     * In the rank,
     * for each bank, it will check if the backward queue has anything.
     * If it has then it will transfer from there to rank's backward queue.
     * Then it will go to add the requests from the current forward queues to appropriate bank's queue
    */
    void updateQueues() {
        // Read the backward queues.
        for (int j = 0; j < noOfBanks; j++) {
            // If the ith bank has something in the backward queue
            moveRequestBw(banks[j].readBackwardQueue, readBackwardQueue, readBwQueueTransferDelay);
            moveRequestBw(banks[j].writeBackwardQueue, writeBackwardQueue, writeBwQueueTransferDelay);

            banks[j].updateQueues();
        }

        // Check the forward queues if any request is ready to move forward

        moveRequestsFw(readForwardQueue, readFwQueueTransferDelay);
        moveRequestsFw(writeForwardQueue, writeFwQueueTransferDelay);
    }

};

class Channel {
    int noOfRanks = 0, noOfBanks = 0, noOfRows = 0, noOfCols = 0;

    public:
    Rank* ranks;

    queue<request*> readForwardQueue;
    queue<request*> writeForwardQueue;
    queue<request*> readBackwardQueue;
    queue<request*> writeBackwardQueue;

    Channel() {}

    void init(int r, int b, int rows, int cols) {
        noOfRanks = r;
        noOfBanks = b;
        noOfRows = rows;
        noOfCols = cols;

        ranks = new Rank[noOfRanks];

        for (int i = 0; i < noOfRanks; i++) {
            ranks[i].init(b, rows, cols);
        }
    }

    void moveRequestsFw(queue<request*>& source, int delay) {
        if (source.size() > maxChannelQSize) maxChannelQSize = source.size();

        if (!source.empty() && (clk - source.front()->localAT) >= delay) {
            // cout << "Moving from Channel to Rank@: " << clk << endl;
            request* r = source.front();
            source.pop();
            r->localAT = clk;
            if (r->RW)
                ranks[r->rank].writeForwardQueue.push(r);
            else 
                ranks[r->rank].readForwardQueue.push(r);

        }
    }

    void updateQueues() {
        // Read the backward queues.
        for (int j = 0; j < noOfBanks; j++) {
            // If the ith rank has something in the backward queue
            moveRequestBw(ranks[j].readBackwardQueue, readBackwardQueue, readBwQueueTransferDelay);
            moveRequestBw(ranks[j].writeBackwardQueue, writeBackwardQueue, writeBwQueueTransferDelay);

            ranks[j].updateQueues();
        }

        moveRequestsFw(readForwardQueue, readFwQueueTransferDelay);
        moveRequestsFw(writeForwardQueue, writeFwQueueTransferDelay);
    }

};

class Memory {
    int noOfChannels, noOfRanks, noOfBanks, noOfParts, noOfRows, noOfCols;

    public:
    Channel* channels;
    vector<int> serviceTime;

    map<vector<int>, vector<int>> mapping;
    vector<vector<long long>> requestsInQueue;

    Memory() {}

    void init(int c, int r, int b, int p, int rows, int cols) {
        noOfChannels = c;
        noOfRanks = r;
        noOfBanks = b;
        noOfRows = rows;
        noOfCols = cols;
        noOfParts = p;

        channels = new Channel[noOfChannels];
        for (int i = 0; i < noOfChannels; i++) {
            channels[i].init(noOfRanks, noOfBanks, noOfRows, noOfCols);
        }
        requestsInQueue.resize(c*r*b, {0, 0, 0, 0});

        int x = 0;
        for (int i = 0; i < noOfChannels; i++) {
            for (int j = 0; j < noOfRanks; j++) {
                for (int k = 0; k < noOfBanks; k++) {
                    requestsInQueue[x][1] = i;
                    requestsInQueue[x][2] = j;
                    requestsInQueue[x][3] = k;
                    x++;
                }
            }
        }

        generateInitialMapping();
    }


    // As of now generating the flat mapping. 
    // Going forward it will read from the configuration file.
    void generateInitialMapping() {
        // Genrating flat mapping 
        for (int c = 0; c < noOfChannels; c++) {
            for (int r = 0; r < noOfRanks; r++) {
                for (int b = 0; b < noOfBanks; b++) {
                    for (int p = 0; p < noOfParts; p++) {
                        vector<int> tmp = {c, r, b, p};
                        mapping[tmp] = tmp;
                    }
                }
            }
        }

    }


    int extractBits(int address, int endBit, int startBit) {
        int mask = (1 << (endBit - startBit + 1)) - 1;
        return (address >> startBit) & mask;
    }

    request* sliceAddress(int address) {
        request* r = new request();
        r->address = address;

        int start = 0, end = colBits-1;
        r->col = extractBits(address, end, start); // Col bits

        start = end+1; end = start + rowBits - 1;
        r->row = extractBits(address, end, start); // Row Bits

        start = end+1; end = start + bankBits - 1;
        r->bank = extractBits(address, end, start); // Bank bits

        start = end+1; end = start + rankBits - 1;
        r->rank = extractBits(address, end, start);

        start = end+1; end = start + channelBits - 1;
        r->channel = extractBits(address, end, start);

        return r;
    }


    void mappingRequest(request* r) {
        vector<int> tmp = {r->channel, r->rank, r->bank, r->part};

        auto itr = mapping[tmp];
        r->channel = tmp[0];
        r->rank = tmp[1];
        r->bank = tmp[2];
        r->part = tmp[3];
    }


    void collectRequests(queue<request*>& source, int delay) {
        if (!source.empty() && (clk - source.front()->localAT) >= delay) {
            // cout << "Popping out from channel queue @: " << clk;
            request* r = source.front();
            source.pop();
            serviceTime.push_back(clk - r->arrivalTime);
            MTF << "Channel: " << r->channel << " Rank: " << r->rank << " Bank: " << r->bank << " Row: " << r->row << endl;
            MTF << hex << r->address << dec << " : " << r->RW << " Arrival clock: " << r->arrivalTime << " Departure clock: " << clk << endl;
        }
    }

    void Access(int address, int RW, bool req) {
        // Read the backward queues from the channels
        for (int j = 0; j < noOfChannels; j++) {
            // cout << "Checking the backward queues for the channels: " << i << endl;
            // Check the read queue
            collectRequests(channels[j].readBackwardQueue, readBwQueueTransferDelay);
            collectRequests(channels[j].writeBackwardQueue, writeBwQueueTransferDelay);

            // Call updateQueue function on all the channels
            channels[j].updateQueues();
        }

        if (req) {
            // cout << "Putting request to channel queue" << clk << endl;
            request* r = sliceAddress(address);

            // Updating the request mapping
            mappingRequest(r);

            r->RW = RW;
            r->arrivalTime = clk;
            r->localAT = clk;
            // cout << "Pusing to channel queue @: " << clk << endl;
            if (RW)
                channels[r->channel].writeForwardQueue.push(r);
            else
                channels[r->channel].readForwardQueue.push(r);
        }
    }

    void checkQSizes() {
        for (int i = 0; i < noOfChannels; i++) {
            for (int j = 0; j < noOfRanks; j++) {
                for (int k = 0; k < noOfBanks; k++) {
                    DATA_COLLECTION << channels[i].ranks[j].banks[k].readForwardQueue.size() << ", ";
                }
            }
		}
        DATA_COLLECTION << endl;
    }

    /**
     * Based on the stats update the mapping to map different parts of the bank to other bank part
    */
    void updateMapping() {
        int x = 0;
        long long sum = 0;
        for (int i = 0; i < noOfChannels; i++) {
            for (int j = 0; j < noOfRanks; j++) {
                for (int k = 0; k < noOfBanks; k++) {
                    requestsInQueue[x][0] = channels[i].ranks[j].banks[k].getNumRequest();
                    sum += requestsInQueue[x][0];
                    cout << x << " : " << requestsInQueue[x][0] << endl;
                    x++;
                }
            }
        }

        cout << "Total requests: " << sum << endl;
        long long avg = sum / x;

        cout << "Average requests per bank: " << avg << endl;

        sort(requestsInQueue.rbegin(), requestsInQueue.rend());

        int l = 0, r = x-1;

        // while (l < r && requestsInQueue[l][0] > avg) {
        //     // replace 1st and 3rd part of the overloaded bank with the 2nd and 4th part of underloaded bank
        // }

    }


    void collectStats() {
        int TotalPageHit = 0, TotalPageMiss = 0;
		int TotalWrite = 0, TotalRead = 0;
		for (int i = 0;i < noOfChannels; i++) {
            for (int j = 0; j < noOfRanks; j++) {
                for (int k = 0; k < noOfBanks; k++) {
                    TotalPageHit += channels[i].ranks[j].banks[k].getNumPageHits();
                    TotalPageMiss += channels[i].ranks[j].banks[k].getNumPageMisses();
                    TotalWrite += channels[i].ranks[j].banks[k].getNumWrite();
                    TotalRead += channels[i].ranks[j].banks[k].getNumRead();
                }
            }
		}
		cout << "\n------------ Memory Statistics-----------\n";
        cout << "=> Total Page Hits = " << TotalPageHit << "\n=> Total Page Miss = " << TotalPageMiss;
		cout << "\n=> Total Reads = " << TotalRead << "\n=> Total Writes = " << TotalWrite<<"\n";

        cout << "\n\n[Rank] The maximum size of the queue during execution is: " << maxRankQSize << endl;
        cout << "\n\n[Channel] The maximum size of the queue during execution is: " << maxChannelQSize << endl;

        double avg = 1.0 * accumulate(serviceTime.begin(), serviceTime.end(), 0LL) / serviceTime.size();

        cout << "\n\nThe average service time of the requests = " << avg << " clock cycles." << endl;
    }
};


/**
 * New simulation
*/
void DRAMSim(Memory* m) {
    // Resetting the clock to zero
    clk = 0;

    // Creating file stream 
    ifstream TF("../smallTrace.txt");

    while (clk < 1e9) {
        clk++;
        long long Address;
        char rw;
        if (TF && clk % 3 == 0) {
            TF >> std::hex >> Address;
            TF >> rw;
            
            long long addr = Address % maxAddr;
            m->Access(addr, (rw == 'W'), true);
            // int addr = rand() % maxAddr;
            // int addr = 8960;
            // m->Access(addr, 0, true);
        } else {
            m->Access(1, 0, false);
        }
    }

    m->updateMapping();
}


int main() {
    // cout << "DRAM request handling with Queue Simulation started..!\n" << endl;
	srand(time(NULL));
    int maxAddr = 1 << 15;

    Memory* m = new Memory();
    m->init(numOfChannel, numOfRankInEachChannel, numOfBankInEachRank, numOfPartsInEachBank, numRows, numCols);
    for (auto r: m->requestsInQueue) {
        cout << r[0] << " : " << r[1] << " : " << r[2] << " : " << r[3] << endl;
    }

    // for (auto [k, v]: m->mapping) {
    //     for (auto i: k) cout << i << " ";
    //     cout << " : ";
    //     for (auto i: v) cout << i << " ";
    //     cout << endl;
    // }


    DRAMSim(m);

    // m->collectStats();

    return 0;
}