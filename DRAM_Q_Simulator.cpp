#include <iostream>
#include <fstream>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <vector>

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
#define numRows 16
#define numCols 256

#define channelBits 1
#define rankBits 1
#define bankBits 1
#define rowBits 4
#define colBits 8

int maxAddr = 1 << 15;

/**
 * Request structure to hold the type of the request and the slicing of the request address.
*/
struct request {
    int address;
    int RW; // 0 - Read, 1 - Write
    int channel; // Channel bits
    int rank; // Rank bits
    int bank; // Bank Bits
    int row; // Row address bits
    int col; // col address bits
    int arrivalTime;
    int departureTime;
    int rClk;

    request() : RW(0), channel(0) {}
};



ofstream MTF("DRAM_Queue_Simulation.txt");


// vector<request*> q;
/*
Queue will store the address and the clock ticking
When the request arrived and departed from the different queue.
1st element in the pair is the address
2nd element in the pair is another pair
    -> containing the arrival time and departure time of the request
*/


void moveRequestBw(vector<request*>& sourceQueue, vector<request*>& targetQueue, int delay) {
    for (int i = 0; i < sourceQueue.size(); i++) {
        sourceQueue[i]->rClk--;
        if (sourceQueue[i]->rClk == 0) {
            sourceQueue[i]->rClk = delay;

            targetQueue.push_back(sourceQueue[i]);

            sourceQueue.erase(sourceQueue.begin() + i);
        }
    }
}


class Bank {
	int Address;
	int RowBufferAddess;
	int PageSize, NumRowPerBank, NumColumnPerRow;
	int PageHit, PageMiss;
	int ReadCtr, WriteCtr;

    int noOfRows, noOfCols;

	public:

    vector<request*> readForwardQueue;
    vector<request*> writeForwardQueue;
    vector<request*> readBackwardQueue;
    vector<request*> writeBackwardQueue;

    // Current request that is being processed by the bank
    vector<request*> processing;
    // Flag indicating whether new request can be processed or not
    bool canProcess; // True - cam, false - can't


	Bank() {}

	void init(int r, int c) {
        noOfRows = r;
        noOfCols = c;
		NumRowPerBank = r;
		NumColumnPerRow = c;
		PageSize = NumColumnPerRow;
        canProcess = true;
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
        if (processing.size() == 0) return;
        processing[0]->rClk--;
        if (processing[0]->rClk == 0) {
            canProcess = true;
            if (processing[0]->RW) {
                processing[0]->rClk = writeBwQueueTransferDelay;
                writeBackwardQueue.push_back(processing[0]);
            } else {
                processing[0]->rClk = readBwQueueTransferDelay;
                readBackwardQueue.push_back(processing[0]);
            }
            processing.pop_back();
        }
    }

    void updateRequests(vector<request*>& sourceQueue, vector<request*>& targetQueue, int delay) {

        for (int i = 0; i < sourceQueue.size(); i++) {
            if (sourceQueue[i]->rClk > 0)
                sourceQueue[i]->rClk--;
            if (sourceQueue[i]->rClk == 0) {
                if (canProcess) {
                    canProcess = false;
                    sourceQueue[i]->rClk = Access(sourceQueue[i]);
                    processing.push_back(sourceQueue[i]);
                    
                    sourceQueue.erase(sourceQueue.begin() + i);
                    
                }
            }
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
        updateRequests(readForwardQueue, readBackwardQueue, readBwQueueTransferDelay);
        updateRequests(writeForwardQueue, writeBackwardQueue, readBwQueueTransferDelay);
    }

    int getNumPageHits() { return PageHit; }
	int getNumPageMisses() { return PageMiss; }
	int getNumWrite() { return WriteCtr; }
	int getNumRead() { return ReadCtr; }
};



class Rank {
    int noOfBanks = 0, noOfRows = 0, noOfCols = 0;

    public:
    Bank* banks;
    vector<request*> readForwardQueue;
    vector<request*> writeForwardQueue;
    vector<request*> readBackwardQueue;
    vector<request*> writeBackwardQueue;

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

    void moveRequestsFw(vector<request*>& source, int delay) {
        for (int i = 0; i < source.size(); i++) {
            source[i]->rClk--;
            // Time to move it to next queue
            if (source[i]->rClk == 0) {
                source[i]->rClk = delay;
                if (source[i]->RW)
                    banks[source[i]->bank].writeForwardQueue.push_back(source[i]);
                else 
                    banks[source[i]->bank].readForwardQueue.push_back(source[i]);

                source.erase(source.begin() + i);
            }
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

    vector<request*> readForwardQueue;
    vector<request*> writeForwardQueue;
    vector<request*> readBackwardQueue;
    vector<request*> writeBackwardQueue;

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

    void moveRequestsFw(vector<request*>& source, int delay) {
        for (int i = 0; i < source.size(); i++) {
            source[i]->rClk--;
            // Time to move it to next queue
            if (source[i]->rClk == 0) {
                source[i]->rClk = delay;
                if (source[i]->RW)
                    ranks[source[i]->rank].writeForwardQueue.push_back(source[i]);
                else 
                    ranks[source[i]->rank].readForwardQueue.push_back(source[i]);

                source.erase(source.begin() + i);
            }
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
    int noOfChannels, noOfRanks, noOfBanks, noOfRows, noOfCols;

    public:
    Channel* channels;

    Memory() {}

    void init(int c, int r, int b, int rows, int cols) {
        noOfChannels = c;
        noOfRanks = r;
        noOfBanks = b;
        noOfRanks = rows;
        noOfCols = cols;

        channels = new Channel[noOfChannels];
        for (int i = 0; i < noOfChannels; i++) {
            channels[i].init(noOfRanks, noOfBanks, noOfRows, noOfCols);
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

    void collectRequests(vector<request*>& source, int clk) {
        for (int i = 0; i < source.size(); i++) {
            source[i]->rClk--;
            if (source[i]->rClk == 0) {
                MTF << hex << source[i]->address << dec << " : " << source[i]->RW << " Arrival clock: " << source[i]->arrivalTime << " Departure clock: " << clk << endl;
                MTF << "Channel: " << source[i]->channel << " Rank: " << source[i]->rank << " Bank: " << source[i]->bank << " Row: " << source[i]->row << endl;
                source.erase(source.begin() + i);
            }
        }
        
    }

    void Access(int address, int RW, int clk, bool req) {
        // Read the backward queues from the channels
        for (int j = 0; j < noOfChannels; j++) {
            // cout << "Checking the backward queues for the channels: " << i << endl;
            // Check the read queue
            collectRequests(channels[j].readBackwardQueue, clk);
            collectRequests(channels[j].writeBackwardQueue, clk);
            

            // Call updateQueue function on all the channels
            channels[j].updateQueues();
        }

        if (req) {
            request* r = sliceAddress(address);
            r->RW = RW;
            r->arrivalTime = clk;
            if (RW) {
                r->rClk = writeFwQueueTransferDelay;
                channels[r->channel].writeForwardQueue.push_back(r);
            } else {
                r->rClk = readFwQueueTransferDelay;
                channels[r->channel].readForwardQueue.push_back(r);
            }
        }
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
    }


};


ifstream TF("smallTrace2.txt");

void DRAMSim(int clk, Memory* m) {
    long long Address;
    char rw;
    if (TF && clk % 3 == 0) {
        TF >> std::hex >> Address;
        TF >> rw;
         
        long long addr = Address % maxAddr;
        m->Access(addr, (rw == 'W'), clk, true);
        // int addr = rand() % maxAddr;
        // m->Access(addr, 0, clk, true);
    } else {
        m->Access(1, 0, clk, false);
    }
}


int main() {
	srand(time(NULL));
    int maxAddr = 1 << 15;

    Memory* m = new Memory();
    m->init(numOfChannel, numOfRankInEachChannel, numOfBankInEachRank, numRows, numCols);

    long long clk = 0;
    while (clk < 1e4) {
        clk++;
        DRAMSim(clk, m);
    }

    m->collectStats();

    return 0;
}