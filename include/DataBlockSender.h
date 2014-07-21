/*
    Network Protocol Foundation Library.
    Copyright (c) 2014, The Network Protocol Company, Inc.
*/

#ifndef PROTOCOL_DATA_BLOCK_SENDER_H
#define PROTOCOL_DATA_BLOCK_SENDER_H

#include "Common.h"

namespace protocol
{
    class Block;
    class Allocator;

    class DataBlockSender
    {
    public:

        DataBlockSender( Allocator & allocator, Block & dataBlock, int fragmentSize, int fragmentsPerSecond );

        ~DataBlockSender();

        void Clear();

        void Update( const TimeBase & timeBase );

        void ProcessAck( int fragmentId );

        int GetBlockSize() const;
        int GetFragmentSize() const { return m_fragmentSize; }
        int GetNumFragments() const { return m_numFragments; }
        int GetNumAckedFragments() const { return m_numAckedFragments; }
        bool SendCompleted() const { return m_numAckedFragments == m_numFragments; }

    protected:

        virtual void SendFragment( int fragmentId, uint8_t * fragmentData, int fragmentBytes ) = 0;

    private:

        Allocator * m_allocator;
        Block * m_dataBlock;
        int m_fragmentSize;
        float m_timeBetweenFragments;
        int m_fragmentIndex;
        int m_numFragments;
        int m_numAckedFragments;
        double m_lastFragmentSendTime;
        uint8_t * m_ackedFragment;        
    };
}

#endif