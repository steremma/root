// @(#)root/tree:$Id$
// Author: Rene Brun   04/06/2006

/*************************************************************************
 * Copyright (C) 1995-2000, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

#ifndef ROOT_TTreeCacheUnzip
#define ROOT_TTreeCacheUnzip


//////////////////////////////////////////////////////////////////////////
//                                                                      //
// TTreeCacheUnzip                                                      //
//                                                                      //
// Specialization of TTreeCache for parallel Unzipping                  //
//                                                                      //
// Fabrizio Furano (CERN) Aug 2009                                      //
// Core TTree-related code borrowed from the previous version           //
//  by Leandro Franco and Rene Brun                                     //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#include "Bytes.h"
#include "TTreeCache.h"
#include "ROOT/TTaskGroup.hxx"
#include <atomic>
#include <queue>
#include <memory>
#include <vector>

class TBasket;
class TBranch;
class TMutex;
class TTree;

class TTreeCacheUnzip : public TTreeCache {

public:
   // We have three possibilities for the unzipping mode:
   // enable, disable and force
   enum EParUnzipMode { kEnable, kDisable, kForce };

   // Unzipping states for a basket:
   enum EUnzipState { kUntouched, kProgress, kFinished };

protected:
   // Unzipping state for baskets
   struct UnzipState {
      // Note: we cannot use std::unique_ptr<std::unique_ptr<char[]>[]> or vector of unique_ptr
      // for fUnzipChunks since std::unique_ptr is not copy constructable.
      // However, in future upgrade we cannot use make_vector in C++14.
      std::unique_ptr<char[]> *fUnzipChunks;     ///<! [fNseek] Individual unzipped chunks. Their summed size is kept under control.
      std::vector<Int_t>       fUnzipLen;        ///<! [fNseek] Length of the unzipped buffers
      std::atomic<Byte_t>     *fUnzipStatus;     ///<! [fNSeek] 

      UnzipState() {
         fUnzipChunks = nullptr;
         fUnzipStatus = nullptr;
      }
      ~UnzipState() {
         if (fUnzipChunks) delete [] fUnzipChunks;
         if (fUnzipStatus) delete [] fUnzipStatus;
      }
      void   Clear(Int_t size);
      Bool_t IsUntouched(Int_t index) const;
      Bool_t IsProgress(Int_t index) const;
      Bool_t IsFinished(Int_t index) const;
      Bool_t IsUnzipped(Int_t index) const;
      void   Reset(Int_t oldSize, Int_t newSize);
      void   SetUntouched(Int_t index);
      void   SetProgress(Int_t index);
      void   SetFinished(Int_t index);
      void   SetMissed(Int_t index);
      void   SetUnzipped(Int_t index, char* buf, Int_t len);
      Bool_t TryUnzipping(Int_t index);
   };

   typedef struct UnzipState UnzipState_t;
   UnzipState_t fUnzipState;

   // Members for paral. managing
   Bool_t      fAsyncReading;
   Bool_t      fEmpty;
   Int_t       fCycle;
   Bool_t      fParallel; ///< Indicate if we want to activate the parallelism (for this instance)

   TMutex     *fIOMutex;

   static TTreeCacheUnzip::EParUnzipMode fgParallel;  ///< Indicate if we want to activate the parallelism

   // IMT TTaskGroup Manager
#ifdef R__USE_IMT
   std::unique_ptr<ROOT::Experimental::TTaskGroup> fUnzipTaskGroup;
#endif

   // Unzipping related members
   Int_t       fNseekMax;         ///<!  fNseek can change so we need to know its max size
   Int_t       fUnzipGroupSize;   ///<!  Min accumulated size of a group of baskets ready to be unzipped by a IMT task
   Long64_t    fUnzipBufferSize;  ///<!  Max Size for the ready unzipped blocks (default is 2*fBufferSize)

   static Double_t fgRelBuffSize; ///< This is the percentage of the TTreeCacheUnzip that will be used

   // Members use to keep statistics
   Int_t       fNFound;           ///<! number of blocks that were found in the cache
   Int_t       fNMissed;          ///<! number of blocks that were not found in the cache and were unzipped
   Int_t       fNStalls;          ///<! number of hits which caused a stall
   Int_t       fNUnzip;           ///<! number of blocks that were unzipped

private:
   TTreeCacheUnzip(const TTreeCacheUnzip &);            //this class cannot be copied
   TTreeCacheUnzip& operator=(const TTreeCacheUnzip &);

   char *fCompBuffer;
   Int_t fCompBufferSize;

   // Private methods
   void  Init();

public:
   TTreeCacheUnzip();
   TTreeCacheUnzip(TTree *tree, Int_t buffersize=0);
   virtual ~TTreeCacheUnzip();

   virtual Int_t       AddBranch(TBranch *b, Bool_t subbranches = kFALSE);
   virtual Int_t       AddBranch(const char *branch, Bool_t subbranches = kFALSE);
   Bool_t              FillBuffer();
   virtual Int_t       ReadBufferExt(char *buf, Long64_t pos, Int_t len, Int_t &loc);
   void                SetEntryRange(Long64_t emin,   Long64_t emax);
   virtual void        StopLearningPhase();
   void                UpdateBranches(TTree *tree);

   // Methods related to the thread
   static EParUnzipMode GetParallelUnzip();
   static Bool_t        IsParallelUnzip();
   static Int_t         SetParallelUnzip(TTreeCacheUnzip::EParUnzipMode option = TTreeCacheUnzip::kEnable);

   // Unzipping related methods
#ifdef R__USE_IMT
   Int_t          CreateTasks();
#endif
   Int_t          GetRecordHeader(char *buf, Int_t maxbytes, Int_t &nbytes, Int_t &objlen, Int_t &keylen);
   virtual Int_t  GetUnzipBuffer(char **buf, Long64_t pos, Int_t len, Bool_t *free);
   Int_t          GetUnzipGroupSize() { return fUnzipGroupSize; }
   virtual void   ResetCache();
   virtual Int_t  SetBufferSize(Int_t buffersize);
   void           SetUnzipBufferSize(Long64_t bufferSize);
   void           SetUnzipGroupSize(Int_t groupSize) { fUnzipGroupSize = groupSize; }
   static void    SetUnzipRelBufferSize(Float_t relbufferSize);
   Int_t          UnzipBuffer(char **dest, char *src);
   Int_t          UnzipCache(Int_t index);

   // Methods to get stats
   Int_t  GetNUnzip() { return fNUnzip; }
   Int_t  GetNMissed(){ return fNMissed; }
   Int_t  GetNFound() { return fNFound; }

   void Print(Option_t* option = "") const;

   // static members
   ClassDef(TTreeCacheUnzip,0)  //Specialization of TTreeCache for parallel unzipping
};

#endif
