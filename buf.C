#include <memory.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <iostream>
#include <stdio.h>
#include "page.h"
#include "buf.h"

#define ASSERT(c)  { if (!(c)) { \
		       cerr << "At line " << __LINE__ << ":" << endl << "  "; \
                       cerr << "This condition should hold: " #c << endl; \
                       exit(1); \
		     } \
                   }

//----------------------------------------
// Constructor of the class BufMgr
//----------------------------------------

BufMgr::BufMgr(const int bufs)
{
    numBufs = bufs;

    bufTable = new BufDesc[bufs];
    memset(bufTable, 0, bufs * sizeof(BufDesc));
    for (int i = 0; i < bufs; i++) 
    {
        bufTable[i].frameNo = i;
        bufTable[i].valid = false;
    }

    bufPool = new Page[bufs];
    memset(bufPool, 0, bufs * sizeof(Page));

    int htsize = ((((int) (bufs * 1.2))*2)/2)+1;
    hashTable = new BufHashTbl (htsize);  // allocate the buffer hash table

    clockHand = bufs - 1;
}


BufMgr::~BufMgr() {

    // flush out all unwritten pages
    for (int i = 0; i < numBufs; i++) 
    {
        BufDesc* tmpbuf = &bufTable[i];
        if (tmpbuf->valid == true && tmpbuf->dirty == true) {

#ifdef DEBUGBUF
            cout << "flushing page " << tmpbuf->pageNo
                 << " from frame " << i << endl;
#endif

            tmpbuf->file->writePage(tmpbuf->pageNo, &(bufPool[i]));
        }
    }

    delete [] bufTable;
    delete [] bufPool;
}


const Status BufMgr::allocBuf(int & frame) 
{
	int start = 0;
	BufDesc* curFrame;
	Status status;
	do {
		advanceClock();
		curFrame = &bufTable[clockHand];
		if (curFrame->valid) {
			if (curFrame->refbit) {
				curFrame->refbit = false;
			}
			else {
				if (!curFrame->pinCnt) {
					if (curFrame->dirty) {
						status = curFrame->file->writePage(curFrame->pageNo, bufPool + clockHand);//flushFile(curFrame->file);
						if (status != OK) {
							return UNIXERR;
						}
					}
					status = hashTable->remove(curFrame->file, curFrame->pageNo);
					frame = clockHand;
					return OK;
				}
			}
		}
		else {
			frame = clockHand;
			return OK;
		}
		start++;
	} while(start < numBufs * 2);
	return BUFFEREXCEEDED;
}

	
const Status BufMgr::readPage(File* file, const int PageNo, Page*& page)
{
	int frameNo;
	BufDesc* curFrame;
	if (hashTable->lookup(file, PageNo, frameNo) == OK) {
		curFrame = &bufTable[frameNo];
		curFrame->pinCnt++;
		curFrame->refbit = true;
		page = bufPool + frameNo;
	}
	else {
		Status status = allocBuf(frameNo);
		if (status != OK) {
			return status;
		}
		page = bufPool + frameNo;
		status = file->readPage(PageNo, page);
		if (status != OK) {
			return status;
		}
		
		if (hashTable->insert(file, PageNo, frameNo) != OK) {
			return HASHTBLERROR;
		}
		curFrame = &bufTable[frameNo];
		curFrame->Set(file, PageNo);
	}
	return OK;
}


const Status BufMgr::unPinPage(File* file, const int PageNo, 
			       const bool dirty) 
{
	int frameNo;
	if (hashTable->lookup(file, PageNo, frameNo) != OK) {
		return HASHNOTFOUND;
	}
	BufDesc &curFrame = bufTable[frameNo];
	if (!curFrame.pinCnt) {
		return PAGENOTPINNED;
	}
	if (dirty) {
		curFrame.dirty = true;
	}
	curFrame.pinCnt--;
	return OK;
}

const Status BufMgr::allocPage(File* file, int& pageNo, Page*& page) 
{
	Status status = file->allocatePage(pageNo);
	if (status != OK) {
		return status;
	}
	
	int frameNo;
	status = allocBuf(frameNo);
	if (status != OK) {
		return status;
	}

	if (hashTable->insert(file, pageNo, frameNo) != OK) {
		return HASHTBLERROR;	
	}
	
	BufDesc &curFrame = bufTable[frameNo];
	curFrame.Set(file, pageNo);

	page = bufPool + frameNo;
	return OK;
}

const Status BufMgr::disposePage(File* file, const int pageNo) 
{
    // see if it is in the buffer pool
    Status status = OK;
    int frameNo = 0;
    status = hashTable->lookup(file, pageNo, frameNo);
    if (status == OK)
    {
        // clear the page
        bufTable[frameNo].Clear();
    }
    status = hashTable->remove(file, pageNo);

    // deallocate it in the file
    return file->disposePage(pageNo);
}

const Status BufMgr::flushFile(const File* file) 
{
  Status status;

  for (int i = 0; i < numBufs; i++) {
    BufDesc* tmpbuf = &(bufTable[i]);
    if (tmpbuf->valid == true && tmpbuf->file == file) {

      if (tmpbuf->pinCnt > 0) {
      	cout << tmpbuf->pinCnt << endl;
      	return PAGEPINNED;
      }
	  

      if (tmpbuf->dirty == true) {
#ifdef DEBUGBUF
	cout << "flushing page " << tmpbuf->pageNo
             << " from frame " << i << endl;
#endif
	if ((status = tmpbuf->file->writePage(tmpbuf->pageNo,
					      &(bufPool[i]))) != OK)
	  return status;

	tmpbuf->dirty = false;
      }

      hashTable->remove(file,tmpbuf->pageNo);

      tmpbuf->file = NULL;
      tmpbuf->pageNo = -1;
      tmpbuf->valid = false;
    }

    else if (tmpbuf->valid == false && tmpbuf->file == file)
      return BADBUFFER;
  }
  
  return OK;
}


void BufMgr::printSelf(void) 
{
    BufDesc* tmpbuf;
  
    cout << endl << "Print buffer...\n";
    for (int i=0; i<numBufs; i++) {
        tmpbuf = &(bufTable[i]);
        cout << i << "\t" << (char*)(&bufPool[i]) 
             << "\tpinCnt: " << tmpbuf->pinCnt;
    
        if (tmpbuf->valid == true)
            cout << "\tvalid\n";
        cout << endl;
    };
}


