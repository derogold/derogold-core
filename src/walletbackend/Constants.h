// Copyright (c) 2018-2021, The DeroGold Developers
// Copyright (c) 2018-2019, The TurtleCoin Developers
//
// Please see the included LICENSE file for more information.

#pragma once

#include <config/CryptoNoteConfig.h>

namespace Constants
{
    /* We use this to check that the file is a wallet file, this bit does
       not get encrypted, and we can check if it exists before decrypting.
       If it isn't, it's not a wallet file. */
    const std::array<char, 64> IS_A_WALLET_IDENTIFIER = {
        {0x49, 0x66, 0x20, 0x49, 0x20, 0x70, 0x75, 0x6c, 0x6c, 0x20, 0x74, 0x68, 0x61, 0x74, 0x20, 0x6f,
         0x66, 0x66, 0x2c, 0x20, 0x77, 0x69, 0x6c, 0x6c, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x64, 0x69, 0x65,
         0x3f, 0x0a, 0x49, 0x74, 0x20, 0x77, 0x6f, 0x75, 0x6c, 0x64, 0x20, 0x62, 0x65, 0x20, 0x65, 0x78,
         0x74, 0x72, 0x65, 0x6d, 0x65, 0x6c, 0x79, 0x20, 0x70, 0x61, 0x69, 0x6e, 0x66, 0x75, 0x6c, 0x2e}};

    /* We use this to check if the file has been correctly decoded, i.e.
       is the password correct. This gets encrypted into the file, and
       then when unencrypted the file should start with this - if it
       doesn't, the password is wrong */
    const std::array<char, 26> IS_CORRECT_PASSWORD_IDENTIFIER = {{0x59, 0x6f, 0x75, 0x27, 0x72, 0x65, 0x20, 0x61, 0x20,
                                                                  0x62, 0x69, 0x67, 0x20, 0x67, 0x75, 0x79, 0x2e, 0x0a,
                                                                  0x46, 0x6f, 0x72, 0x20, 0x79, 0x6f, 0x75, 0x2e}};

    /* The number of iterations of PBKDF2 to perform on the wallet
       password. */
    const uint64_t PBKDF2_ITERATIONS = 500000;

    /* What version of the file format are we on (to make it easier to
       upgrade the wallet format in the future) */
    const uint16_t WALLET_FILE_FORMAT_VERSION = 0;

    /* How many of the blocks waiting to be processed the downloader names when
       it asks for more. */
    const size_t LAST_KNOWN_BLOCK_HASHES_SIZE = 50;

    /* How many processed block hashes the wallet keeps, to resume from after a
       reorg. Held densely - one per block - and thinned only on the way out,
       so depth costs storage rather than request size. */
    const size_t RECENT_BLOCK_HASHES_SIZE = 200;

    /* The first this many of them go into the locator one after another. A
       reorg is nearly always shallow, so the exact resume point is worth
       naming precisely near the tip. */
    const size_t LOCATOR_DENSE_COUNT = 10;

    /* Save a block hash checkpoint every BLOCK_HASH_CHECKPOINTS_INTERVAL
       blocks */
    const uint32_t BLOCK_HASH_CHECKPOINTS_INTERVAL = 5000;

    /* And keep at most this many of them. At the interval above that is half a
       million blocks of reach, far past any reorg that could really happen,
       and it stops a list that grew by one entry per 5000 blocks forever from
       being shipped in full on every sync request. */
    const size_t BLOCK_HASH_CHECKPOINTS_MAX = 100;

    /* The amount of blocks since an input has been spent that we remove it
       from the container */
    const uint64_t PRUNE_SPENT_INPUTS_INTERVAL = CryptoNote::parameters::EXPECTED_NUMBER_OF_BLOCKS_PER_DAY * 2;

    /* When we get the global indexes, we pass in a range of blocks, to obscure
       which transactions we are interested in - the ones that belong to us.
       To do this, we get the global indexes for all transactions in a range.

       For example, if we want the global indexes for a transaction in block
       17, we get all the indexes from block 10 to block 20.

       This value determines how many blocks to take from. */
    const uint64_t GLOBAL_INDEXES_OBSCURITY = 10;

    /* Amount of blocks to take in one chunk from the block downloader, and
       then split into threads and process. Too large will result in large
       jumps in the sync height, but should offer better performance from a
       decrease in locking of data structures. */
    const uint64_t BLOCK_PROCESSING_CHUNK = 500;
} // namespace Constants
