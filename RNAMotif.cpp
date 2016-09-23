// ==========================================================================
//                                  RNAMotif
// ==========================================================================
// Copyright (c) 2006-2013, Knut Reinert, FU Berlin
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//     * Neither the name of Knut Reinert or the FU Berlin nor the names of
//       its contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL KNUT REINERT OR THE FU BERLIN BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
// OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
// DAMAGE.
//
// ==========================================================================
// Author: Benjamin Strauch
// ==========================================================================

// SeqAn headers
#include <seqan/basic.h>
#include <seqan/sequence.h>
#include <seqan/arg_parse.h>

// C++ headers
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

// App headers
#include "RNAlib_utils.h"
#include "IPknot_utils.h"
#include "motif.h"

// reading the Stockholm format
#include "stockholm_file.h"
#include "stockholm_io.h"


// -----------

#ifdef _WIN32
#include <Windows.h>
#else
#include <sys/time.h>
#include <ctime>
#endif

/* Returns the amount of milliseconds elapsed since the UNIX epoch. Works on both
 * windows and linux. */

uint64_t GetTimeMs64()
{
#ifdef _WIN32
 /* Windows */
 FILETIME ft;
 LARGE_INTEGER li;

 /* Get the amount of 100 nano seconds intervals elapsed since January 1, 1601 (UTC) and copy it
  * to a LARGE_INTEGER structure. */
 GetSystemTimeAsFileTime(&ft);
 li.LowPart = ft.dwLowDateTime;
 li.HighPart = ft.dwHighDateTime;

 uint64_t ret = li.QuadPart;
 ret -= 116444736000000000LL; /* Convert from file time to UNIX epoch time. */
 ret /= 10000; /* From 100 nano seconds (10^-7) to 1 millisecond (10^-3) intervals */

 return ret;
#else
 /* Linux */
 struct timeval tv;

 gettimeofday(&tv, NULL);

 uint64_t ret = tv.tv_usec;
 /* Convert from micro seconds (10^-6) to milliseconds (10^-3) */
 ret /= 1000;

 /* Adds the seconds (10^0) after converting them to milliseconds (10^-3) */
 ret += (tv.tv_sec * 1000);

 return ret;
#endif
}


// ==========================================================================
// Classes
// ==========================================================================

// --------------------------------------------------------------------------
// Class AppOptions
// --------------------------------------------------------------------------

// This struct stores the options from the command line.
//
// You might want to rename this to reflect the name of your app.

struct AppOptions
{
    // Verbosity level.  0 -- quiet, 1 -- normal, 2 -- verbose, 3 -- very verbose.
    int verbosity;
    bool constrain;
    bool pseudoknot;

    // The first (and only) argument of the program is stored here.
    seqan::CharString rna_file;
    seqan::CharString out_file;

    AppOptions() :
        verbosity(1),
		constrain(0),
		pseudoknot(0)
    {}
};

// ==========================================================================
// Functions
// ==========================================================================

// --------------------------------------------------------------------------
// Function parseCommandLine()
// --------------------------------------------------------------------------

seqan::ArgumentParser::ParseResult
parseCommandLine(AppOptions & options, int argc, char const ** argv)
{
    // Setup ArgumentParser.
    seqan::ArgumentParser parser("RNAMotif");
    // Set short description, version, and date.
    setShortDescription(parser, "RNA motif generator");
    setVersion(parser, "0.1");
    setDate(parser, __DATE__);

    // Define usage line and long description.
    addUsageLine(parser, "[\\fIOPTIONS\\fP] <\\fISEED ALIGNMENT\\fP> <\\fIMOTIF OUTPUT\\fP>");
    addDescription(parser, "Generate a searchable RNA motif from a seed alignment.");

    // We require one argument.
    addArgument(parser, seqan::ArgParseArgument(seqan::ArgParseArgument::STRING, "INPUT FILE"));
    addArgument(parser, seqan::ArgParseArgument(seqan::ArgParseArgument::STRING, "MOTIF FILE"));

    addOption(parser, seqan::ArgParseOption("ps", "pseudoknot", "Predict structure with IPknot to include pseuoknots."));
    addOption(parser, seqan::ArgParseOption("co", "constrain", "Constrain individual structures with the seed consensus structure."));
    addOption(parser, seqan::ArgParseOption("q", "quiet", "Set verbosity to a minimum."));
    addOption(parser, seqan::ArgParseOption("v", "verbose", "Enable verbose output."));
    addOption(parser, seqan::ArgParseOption("vv", "very-verbose", "Enable very verbose output."));

    // Add Examples Section.
    addTextSection(parser, "Examples");
    addListItem(parser, "\\fBRNAMotif\\fP \\fB-v\\fP \\fItext\\fP",
                "Call with \\fITEXT\\fP set to \"text\" with verbose output.");

    // Parse command line.
    seqan::ArgumentParser::ParseResult res = seqan::parse(parser, argc, argv);

    // Only extract  options if the program will continue after parseCommandLine()
    if (res != seqan::ArgumentParser::PARSE_OK)
        return res;

    options.constrain  = isSet(parser, "constrain");
    options.pseudoknot = isSet(parser, "pseudoknot");

    // Extract option values.
    if (isSet(parser, "quiet"))
        options.verbosity = 0;
    if (isSet(parser, "verbose"))
        options.verbosity = 2;
    if (isSet(parser, "very-verbose"))
        options.verbosity = 3;
    seqan::getArgumentValue(options.rna_file, parser, 0);
    seqan::getArgumentValue(options.out_file, parser, 1);

    return seqan::ArgumentParser::PARSE_OK;
}

// --------------------------------------------------------------------------
// Function main()
// --------------------------------------------------------------------------

// Program entry point.

int main(int argc, char const ** argv)
{
    // Parse the command line.
    seqan::ArgumentParser parser;
    AppOptions options;
    seqan::ArgumentParser::ParseResult res = parseCommandLine(options, argc, argv);
	
    // If there was an error parsing or built-in argument parser functionality
    // was triggered then we exit the program.  The return code is 1 if there
    // were errors and 0 if there were none.
    if (res != seqan::ArgumentParser::PARSE_OK)
        return res == seqan::ArgumentParser::PARSE_ERROR;

    std::cout << "RNA motif generator\n"
              << "===============\n\n";

    // Print the command line arguments back to the user.
    if (options.verbosity > 0)
    {
        std::cout << "__OPTIONS____________________________________________________________________\n"
                  << '\n'
                  << "VERBOSITY\t" << options.verbosity << '\n'
                  << "CONSTRAINT\t" << options.constrain << '\n'
				  << "PSEUDOKNOTS\t" << options.pseudoknot << '\n'
				  << "RNA      \t" << options.rna_file << '\n'
                  << "OUTPUT   \t" << options.out_file << "\n\n";
    }

    std::vector<seqan::StockholmRecord<seqan::Rna>> test_records;

    uint64_t start = GetTimeMs64();

    seqan::StockholmFileIn stockFileIn;
    seqan::open(stockFileIn, seqan::toCString(options.rna_file));

    while (!seqan::atEnd(stockFileIn)){
    	seqan::StockholmRecord<seqan::Rna> test_record;
		seqan::readRecord(test_record, stockFileIn);
		test_records.push_back(test_record);
    }

    std::cout << test_records.size() << " records read\n";
    std::cout << "Time: " << GetTimeMs64() - start << "ms \n";

    std::vector<seqan::StockholmRecord<seqan::Rna> > records;

    start = GetTimeMs64();

	read_Stockholm_file(seqan::toCString(options.rna_file), records);

	std::cout << "Time: " << GetTimeMs64() - start << "ms \n";

	std::vector<Motif> motifs(records.size());

    // read the stockholm alignment
	//StockholmRecord<seqan::Rna> record = records[0];
	//std::cout << record.alignment << "\n";

	#pragma omp parallel for //schedule(dynamic,4)
	for (size_t k=0; k < records.size(); ++k){
		seqan::StockholmRecord<seqan::Rna> const &record = records[k];


		std::cout << record.header.at("AC") << " : " << record.header.at("ID") << "\n";

		int seq_len = record.seqences.begin()->second.length();
		if (seq_len > 1000){
			std::cout << "Alignment has length " << seq_len << " > 1000 .. skipping.\n";
			continue;
		}

		// convert Rfam WUSS structure to normal brackets to get a constraint
		char *constraint_bracket = NULL;
		if (options.constrain){
			constraint_bracket = new char[record.seqence_information.at("SS_cons").length() + 1];
			WUSStoPseudoBracket(record.seqence_information.at("SS_cons"), constraint_bracket);
		}


		Motif rna_motif;
		rna_motif.header = record.header;
		rna_motif.seedAlignment = record.alignment;
		rna_motif.interactionGraphs.resize(record.seqences.size());
		rna_motif.interactionPairs.resize(record.seqences.size());

		// build interaction graphs for each sequence
		int i = 0;
		for (auto elem : record.seqences)
		{
			//createInteractions(rna_motif.interactionGraphs[i], rna_motif.interactionPairs[i], elem.second, constraint_bracket);
			i++;
		}

		// create structure for the whole multiple alignment
		DEBUG_MSG("Rfam:   " << record.seqence_information.at("SS_cons"));
		if (options.pseudoknot)
			getConsensusStructure(record, rna_motif.consensusStructure, constraint_bracket, IPknotFold());
		else
			getConsensusStructure(record, rna_motif.consensusStructure, constraint_bracket, RNALibFold());

		structurePartition(rna_motif);

		motifs[k] = rna_motif;

		if (options.constrain)
			free(constraint_bracket);
	}

	std::cout << std::endl;

    return 0;
}
