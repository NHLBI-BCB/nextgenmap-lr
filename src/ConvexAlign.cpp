/**
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * Contact: philipp.rescheneder@univie.ac.at
 */

#include "ConvexAlign.h"

#include <cmath>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>

//TODO: remove
#define pRef pBuffer1
#define pQry pBuffer2

namespace Convex {

int alignmentId = 0;

int NumberOfSetBits(uint32_t i) {
	// Java: use >>> instead of >>
	// C or C++: use uint32_t
	i = i - ((i >> 1) & 0x55555555);
	i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
	return (((i + (i >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;
}

ConvexAlign::ConvexAlign(int const stdOutMode,
		float const match,
		float const mismatch,
		float const gapOpen,
		float const gapExtend,
		float const gapExtendMin,
		float const gapDecay):
		maxBinaryCigarLength(200000), pacbioDebug(false), stdoutPrintAlignCorridor(stdOutMode) {
	mat = match;
	mis = mismatch;
	gap_open_read = gapOpen;
	gap_open_ref = gapOpen;
	gap_ext = gapExtend;
	gap_decay = gapDecay;
	gap_ext_min = gapExtendMin;
	matrix = new AlignmentMatrix();

	binaryCigar = new int[maxBinaryCigarLength];
}

ConvexAlign::~ConvexAlign() {

	delete matrix;
	matrix = 0;

	delete[] binaryCigar;
	binaryCigar = 0;
}

int ConvexAlign::printCigarElement(char const op, int const length,
		char * cigar) {
	int offset = 0;
	offset = sprintf(cigar, "%d%c", length, op);

	return offset;
}

void addPosition(Align & result, int & nmIndex, int posInRef, int posInRead,
		int Yi) {
	if (posInRead > 16 && posInRef > 16) {
		result.nmPerPosition[nmIndex].readPosition = posInRead - 16;
		result.nmPerPosition[nmIndex].refPosition = posInRef - 16;
		result.nmPerPosition[nmIndex].nm = Yi;
		nmIndex += 1;
	}
}

int ConvexAlign::convertCigar(char const * const refSeq, Align & result,
		FwdResults & fwdResults, int const externalQStart,
		int const externalQEnd) {

	//*********************//
	//Inversion detection init
	//*********************//
	uint buffer = 0;
	int posInRef = 0;
	int posInRead = 0;

	//*********************//
	//General init
	//*********************//

	int nmPerPositionLength = (matrix->getHeight() + 1) * 2;
	result.nmPerPosition = new PositionNM[nmPerPositionLength];
	int nmIndex = 0;
	int exactAlignmentLength = 0;

	int finalCigarLength = 0;

	int cigar_offset = 0;
	int md_offset = 0;

	int binaryCigarIndex = fwdResults.alignment_offset;

	//*********************//
	// Set QStart
	//*********************//
	result.QStart = ((binaryCigar[binaryCigarIndex] >> 4) + externalQStart);
	if (result.QStart > 0) {
		cigar_offset += printCigarElement('S', result.QStart,
				result.pRef + cigar_offset);
		finalCigarLength += result.QStart;
	}
	posInRead = binaryCigar[binaryCigarIndex] >> 4;

	//Positions in read and ref for start of alignment
	result.firstPosition.refPosition = posInRef;
	result.firstPosition.readPosition = posInRead; //QStart of aligned sequence, but not for full read (like result.QStart)

	//*********************//
	// Translate CIGAR to char and compute MD
	//*********************//
	int matches = 0;
	int alignmentLength = 0;

	int cigar_m_length = 0;
	int md_eq_length = 0;
	int ref_index = 0;

	int overallMatchCount = 0;

	//uint const maxIndelLength = 5;
	uint const maxIndelLength = 1;

	//Inversion detection Arndt
	int Yi = 0;

	for (int j = binaryCigarIndex + 1; j < (maxBinaryCigarLength - 1); ++j) {
		int cigarOp = binaryCigar[j] & 15;
		int cigarOpLength = binaryCigar[j] >> 4;

		alignmentLength += cigarOpLength;
//		fprintf(stderr, "Ref index: %d\n", ref_index);
		switch (cigarOp) {
		case CIGAR_X:
			cigar_m_length += cigarOpLength;

			//Produces: 	[0-9]+(([A-Z]+|\^[A-Z]+)[0-9]+)*
			//instead of: 	[0-9]+(([A-Z]|\^[A-Z]+)[0-9]+)*
			for (int k = 0; k < cigarOpLength; ++k) {
//				fprintf(stderr, "Ref base: %c (%d) > \n", refSeq[ref_index], ref_index);
				md_offset += sprintf(result.pQry + md_offset, "%d", md_eq_length);
				md_eq_length = 0;
				md_offset += sprintf(result.pQry + md_offset, "%c",
						refSeq[ref_index++]);

				buffer = buffer << 1;
				buffer = buffer | 1;

				//				Yi = std::max(0, Yi + 1);
				Yi = NumberOfSetBits(buffer);
				addPosition(result, nmIndex, posInRef++, posInRead++, Yi);
			}

			exactAlignmentLength += cigarOpLength;

			break;
		case CIGAR_EQ:
			cigar_m_length += cigarOpLength;
			md_eq_length += cigarOpLength;
			matches += cigarOpLength;

			overallMatchCount += cigarOpLength;

			for (int k = 0; k < cigarOpLength; ++k) {
//				fprintf(stderr, "Ref base: %c (%d)\n", refSeq[ref_index + k], ref_index + k);
				buffer = buffer << 1;
				//				Yi = std::max(0, Yi - 1);
				Yi = NumberOfSetBits(buffer);
				addPosition(result, nmIndex, posInRef++, posInRead++, Yi);
			}
			ref_index += cigarOpLength;

			exactAlignmentLength += cigarOpLength;

			break;
		case CIGAR_D:
			if (cigar_m_length > 0) {
				cigar_offset += printCigarElement('M', cigar_m_length,
						result.pRef + cigar_offset);
				finalCigarLength += cigar_m_length;
				cigar_m_length = 0;
			}
			cigar_offset += printCigarElement('D', cigarOpLength,
					result.pRef + cigar_offset);

			md_offset += sprintf(result.pQry + md_offset, "%d", md_eq_length);
			md_eq_length = 0;
			result.pQry[md_offset++] = '^';

			for (int k = 0; k < cigarOpLength; ++k) {
//				fprintf(stderr, "Ref base: %c (%d) > \n", refSeq[ref_index], ref_index);
				result.pQry[md_offset++] = refSeq[ref_index++];
				buffer = buffer << 1;
				if (k < maxIndelLength) {
					buffer = buffer | 1;
					Yi = std::max(0, Yi + 1);
				}
				addPosition(result, nmIndex, posInRef++, posInRead, Yi);
			}

			exactAlignmentLength += cigarOpLength;

			break;
		case CIGAR_I:
			if (cigar_m_length > 0) {
				cigar_offset += printCigarElement('M', cigar_m_length,
						result.pRef + cigar_offset);
				finalCigarLength += cigar_m_length;
				cigar_m_length = 0;
			}

			cigar_offset += printCigarElement('I', cigarOpLength,
					result.pRef + cigar_offset);
			finalCigarLength += cigarOpLength;

			for (int k = 0; k < cigarOpLength; ++k) {
				buffer = buffer << 1;
				if (k < maxIndelLength) {
					buffer = buffer | 1;
					Yi = std::max(0, Yi + 1);
				}
				//				addPosition(result, nmIndex, posInRef++, posInRead, Yi);
				posInRead += 1;
			}

			exactAlignmentLength += cigarOpLength;

			break;
		default:
			fprintf(stderr, "Invalid cigar string: %d\n", cigarOp);
			throw 1;
		}
	}

	//*********************//
	//Print last element
	//*********************//
	md_offset += sprintf(result.pQry + md_offset, "%d", md_eq_length);
	if (cigar_m_length > 0) {
		cigar_offset += printCigarElement('M', cigar_m_length,
				result.pRef + cigar_offset);
		finalCigarLength += cigar_m_length;
		cigar_m_length = 0;
	}

	//*********************//
	//Set QEnd
	//*********************//
	result.QEnd = ((binaryCigar[maxBinaryCigarLength - 1] >> 4) + externalQEnd);
	if (result.QEnd > 0) {
		cigar_offset += printCigarElement('S', result.QEnd,
				result.pRef + cigar_offset);
	}
	finalCigarLength += result.QEnd;

	result.Identity = matches * 1.0f / alignmentLength;
	result.pRef[cigar_offset] = '\0';
	result.pQry[md_offset] = '\0';

	result.NM = alignmentLength - matches;
	result.alignmentLength = exactAlignmentLength;

	if (nmPerPositionLength < exactAlignmentLength) {
		fprintf(stderr, "Alignmentlength (%d) < exactAlingmentlength (%d)\n",
				nmPerPositionLength, exactAlignmentLength);
	}
	//	fprintf(stderr, "\n==== Matches: %d of %d ====\n", overallMatchCount,
	//			posInRead);

	//Positions in read and ref for end of alignment
	result.lastPosition.refPosition = posInRef;
	result.lastPosition.readPosition = posInRead;
	//	extData[edIndex++] = posInRef;
	//	extData[edIndex++] = posInRead; //QEnd of aligned sequence, but not for full read (like result.QEnd)

	return finalCigarLength;

}

bool ConvexAlign::revBacktrack(char const * const refSeq,
		char const * const qrySeq, FwdResults & fwdResults, int readId) {

	if (fwdResults.best_read_index <= 0) {
		return false;
	}

	bool validAlignment = true;

	int binaryCigarIndex = maxBinaryCigarLength - 1;

	int cigar_element = CIGAR_S;
	int cigar_element_length = fwdResults.qend;

	int cigarStringLength = fwdResults.qend;

	int x = fwdResults.best_ref_index;
	int y = fwdResults.best_read_index;

//	AlignmentMatrix::MatrixElement currentElement;

	char currentElement = 0;

//	while ((currentElement = matrix->getElement(x, y)->direction) != CIGAR_STOP) {
	while ((currentElement = *matrix->getDirection(x, y)) != CIGAR_STOP) {

//		if (x < 0 || y < 0 || x > matrix->getWidth()
//				|| y > matrix->getHeight()) {
//			fprintf(stderr, "Error in backtracking. x, y indexing error.\n");
//			return false;
//		}

		//TODO: add corridor check. backtracking path too close to corridor end
		if (!matrix->validPath(x, y)) {
			if (pacbioDebug) {
				fprintf(stderr, "Corridor probably too small\n");
			}
			return false;
		}

		if (stdoutPrintAlignCorridor) {
			printf("%d\t%d\t%d\t%d\t%d\n", readId, alignmentId, x, y, 2);
		}

		if (currentElement == CIGAR_X || currentElement == CIGAR_EQ) {
			y -= 1;
			x -= 1;

			cigarStringLength += 1;
		} else if (currentElement == CIGAR_I) {
			y -= 1;
			cigarStringLength += 1;
		} else if (currentElement == CIGAR_D) {
			x -= 1;
		} else {
			fprintf(stderr,
					"Error in backtracking. Invalid CIGAR operation found\n");
//			throw "";
			return false;
		}

		if (currentElement == cigar_element) {
			cigar_element_length += 1;
		} else {
			binaryCigar[binaryCigarIndex--] = (cigar_element_length << 4
					| cigar_element);

//			if (binaryCigarIndex < 0) {
//				fprintf(stderr,
//						"Error in backtracking. CIGAR buffer not long enough\n");
//				throw "";
//			}

			cigar_element = currentElement;
			cigar_element_length = 1;
		}
	}
	// Add last element to binary cigar
	binaryCigar[binaryCigarIndex--] =
			(cigar_element_length << 4 | cigar_element);
	if (binaryCigarIndex < 0) {
		fprintf(stderr,
				"Error in backtracking. CIGAR buffer not long enough\n");
		return false;
	}

	binaryCigar[binaryCigarIndex--] = ((y + 1) << 4 | CIGAR_S);
	cigarStringLength += (y + 1);

	fwdResults.ref_position = x + 1;
	fwdResults.qstart = (y + 1);
	//qend was set by "forward" kernel
	fwdResults.alignment_offset = binaryCigarIndex + 1;

	if (matrix->getHeight() != cigarStringLength) {
		fprintf(stderr, "Error read length != cigar length: %d vs %d\n",
				matrix->getHeight(), cigarStringLength);
		validAlignment = false;
	}

	return validAlignment;

}

int ConvexAlign::GetScoreBatchSize() const {
	return 0;
}
int ConvexAlign::GetAlignBatchSize() const {
	return 0;
}

int ConvexAlign::BatchAlign(int const mode, int const batchSize,
		char const * const * const refSeqList,
		char const * const * const qrySeqList, Align * const results,
		void * extData) {

	throw "Not implemented";

	fprintf(stderr, "Unsupported alignment mode %i\n", mode);
	return 0;
}

int ConvexAlign::SingleAlign(int const mode, CorridorLine * corridorLines,
		int const corridorHeight, char const * const refSeq,
		char const * const qrySeq, Align & align, int const externalQStart,
		int const externalQEnd, void * extData) {

	alignmentId = align.svType;

	align.Score = -1.0f;
//	return 0;

	int finalCigarLength = -1;

	try {

		int const refLen = strlen(refSeq);
		int const qryLen = strlen(qrySeq);

		matrix->prepare(refLen, qryLen, corridorLines, corridorHeight);

		align.pBuffer2[0] = '\0';

		FwdResults fwdResults;

		// Debug: rscript convex-align-vis.r
		if (stdoutPrintAlignCorridor) {
			printf("%d\t%d\t%d\t%d\t%d\n", mode, alignmentId, refLen, qryLen,
					-1);
		}

		AlignmentMatrix::Score score = fwdFillMatrix(refSeq, qrySeq, fwdResults,
				mode);
////	fprintf(stderr, "fill: %f\n", t1.ET());
//
//		//	matrix->printMatrix(refSeq, qrySeq);
//		//	fprintf(stderr, "Best y: %d, Best x: %d, Score: %d, QEnd: %d\n",
//		//			fwdResults.best_read_index, fwdResults.best_ref_index,
//		//			fwdResults.max_score, fwdResults.qend);
//
////	Timer t2;
////	t2.ST();
		bool validAlignment = revBacktrack(refSeq, qrySeq, fwdResults, mode);
////	fprintf(stderr, "fill: %f\n", t2.ET());
//
		if (validAlignment) {
//
////		Timer t3;
////		t3.ST();
//			//		fprintf(stderr, "QStart: %d, Ref offset: %d, Binary cigar offset: %d\n",
//			//				fwdResults.qstart, fwdResults.ref_position,
//			//				fwdResults.alignment_offset);
			if(pacbioDebug) {
				fprintf(stderr, "Ref offset: %d\n", fwdResults.ref_position);
			}
			finalCigarLength = convertCigar(refSeq + fwdResults.ref_position, align, fwdResults,
					externalQStart, externalQEnd);
//
			align.PositionOffset = fwdResults.ref_position;
			align.Score = score;
////		fprintf(stderr, "conv: %f\n", t3.ET());
		}
//
		if (stdoutPrintAlignCorridor) {
			printf("%d\t%d\t%d\t%d\t%d\n", mode, alignmentId, (int) score,
					finalCigarLength, -3);
		}
	} catch (...) {
		align.Score = -1.0f;
		finalCigarLength = -1;
	}

	matrix->clean();
//	alignmentId += 1;
//	align.Score = -1.0f;
	return finalCigarLength;
}

int ConvexAlign::SingleAlign(int const mode, int const corridor,
		char const * const refSeq, char const * const qrySeq, Align & align,
		void * extData) {

//	int corridorWidth = corridor;
//	int const qryLen = strlen(qrySeq);
//
//	int corridorHeight = qryLen;
//	CorridorLine * corridorLines = new CorridorLine[corridorHeight];
//
//	for (int i = 0; i < corridorHeight; ++i) {
//		corridorLines[i].offset = i - corridorWidth / 2;
//		corridorLines[i].length = corridorWidth;
//	}
//
//	int returnValue = SingleAlign(mode, corridorLines, corridorHeight, refSeq,
//			qrySeq, align, extData);
//	delete[] corridorLines;
//	corridorLines = 0;
//
//	return returnValue;
	fprintf(stderr, "SingleAlign not implemented");
	throw "Not implemented";
}

AlignmentMatrix::Score ConvexAlign::fwdFillMatrix(char const * const refSeq,
		char const * const qrySeq, FwdResults & fwdResult, int readId) {

	AlignmentMatrix::Score curr_max = -1.0f;

	for (int y = 0; y < matrix->getHeight(); ++y) {

		matrix->prepareLine(y);

		int xOffset = matrix->getCorridorOffset(y);

		// Debug: rscript convex-align-vis.r
		if (stdoutPrintAlignCorridor) {
			printf("%d\t%d\t%d\t%d\t%d\n", readId, alignmentId, xOffset, y, 0);
			printf("%d\t%d\t%d\t%d\t%d\n", readId, alignmentId,
					xOffset + matrix->getCorridorLength(y), y, 1);
		}

		char const read_char_cache = qrySeq[y];

		for (int x = xOffset; x < (xOffset + matrix->getCorridorLength(y));
				++x) {
			if (x >= matrix->getWidth() || x < 0) {
				continue;
			}

//			AlignmentMatrix::MatrixElement const & diag =
//					matrix->getElementConst(x - 1, y - 1);
			AlignmentMatrix::Score diag_score = matrix->getScore(x - 1, y - 1);
			AlignmentMatrix::MatrixElement const & up = *matrix->getElement(x,
					y - 1);
			AlignmentMatrix::MatrixElement const & left = *matrix->getElement(
					x - 1, y);

			bool const eq = read_char_cache == refSeq[x];
			AlignmentMatrix::Score const diag_cell = diag_score
					+ ((eq) ? mat : ((refSeq[x] != 'x') ? mis : mis * 100.0f));

			AlignmentMatrix::Score up_cell = 0;
			AlignmentMatrix::Score left_cell = 0;

			int ins_run = 0;
			int del_run = 0;

			if (up.direction == CIGAR_I) {
				ins_run = up.indelRun;
				if (up.score == 0) {
					up_cell = 0;
				} else {
					up_cell = up.score
							+ std::min(gap_ext_min,
									gap_ext + ins_run * gap_decay);
				}
			} else {
				up_cell = up.score + gap_open_read;
			}

			if (left.direction == CIGAR_D) {
				del_run = left.indelRun;
				if (left.score == 0) {
					left_cell = 0;
				} else {
					left_cell = left.score
							+ std::min(gap_ext_min,
									gap_ext + del_run * gap_decay);
				}
			} else {
				left_cell = left.score + gap_open_ref;
			}

			//find max
			AlignmentMatrix::Score max_cell = 0;
			max_cell = std::max(left_cell, max_cell);
			max_cell = std::max(diag_cell, max_cell);
			max_cell = std::max(up_cell, max_cell);

			AlignmentMatrix::MatrixElement * current = matrix->getElementEdit(x,
					y);
			char & currentDirection = *matrix->getDirection(x, y);

			if (del_run > 0 && max_cell == left_cell) {
				current->score = max_cell;
				current->direction = CIGAR_D;
				currentDirection = CIGAR_D;
				current->indelRun = del_run + 1;
			} else if (ins_run > 0 && max_cell == up_cell) {
				current->score = max_cell;
				current->direction = CIGAR_I;
				currentDirection = CIGAR_I;
				current->indelRun = ins_run + 1;
			} else if (max_cell == diag_cell) {
				current->score = max_cell;
				if (eq) {
					current->direction = CIGAR_EQ;
					currentDirection = CIGAR_EQ;
				} else {
					current->direction = CIGAR_X;
					currentDirection = CIGAR_X;
				}
				current->indelRun = 0;
			} else if (max_cell == left_cell) {
				current->score = max_cell;
				current->direction = CIGAR_D;
				currentDirection = CIGAR_D;
				current->indelRun = 1;
			} else if (max_cell == up_cell) {
				current->score = max_cell;
				current->direction = CIGAR_I;
				currentDirection = CIGAR_I;
				current->indelRun = 1;
			} else {
				current->score = 0;
				current->direction = CIGAR_STOP;
				currentDirection = CIGAR_STOP;
				current->indelRun = 0;
			}

			if (max_cell > curr_max) {
				curr_max = max_cell;
				fwdResult.best_ref_index = x;
				fwdResult.best_read_index = y;
				fwdResult.max_score = curr_max;
			}

		}

	}
	fwdResult.qend = (matrix->getHeight() - fwdResult.best_read_index) - 1;
	if (matrix->getHeight() == 0) {
		fwdResult.best_read_index = fwdResult.best_ref_index = 0;
	}

	return curr_max;
}

int ConvexAlign::BatchScore(int const mode, int const batchSize,
		char const * const * const refSeqList,
		char const * const * const qrySeqList, float * const results,
		void * extData) {
	throw "Not implemented";
	return 0;

}

}

// g++ -I ../include/ ConvexAlign.cpp -o ConvexAlign && ./ConvexAlign
//int main(int argc, char **argv) {
//
//	IAlignment * aligner = new Convex::ConvexAlign(0);
//
////	char const * ref =
////			"TCAAGATGCCATTGTCCCCCCGGCCTCCTGCTGCTGCTGCTCTCCGGGGCCACGGCCACCGCTGCTCCTGCC";
//	char const * ref =
//			"AGATACATTGTTACTAACTAAGGCCCTACACCAGCAAAGATACATTGTTACTAAGTAAGGCCCTGCACCAACACAG";
//////	char const * qry =
//////			"TCAAGATGCCAATTGTCCCCCGGCCCCCCTGCTGCTGCTGCTCTCCGGGGCCACGGCCACCGCTGCCCTGCC";
////	char const * qry =
////				"CGGGGCCACGGCCACCGCTGCCCTGCC";
////
//	char const * qry =
//			"CCAGAGCAAGTTCCCGGGGGGTGGCGCATAGATGCAGCGGTCTGGGTTCTGTGGCTGGGGGAGTTGGCTTGGGGTCCGTGGCTTGGGTCAGTCTTCAAGAGCCACCCTGGGTGGCACTCCAGGTTCTCTTGGTCTGGGGGGAATTACCCTGACCCCAGCGGTTTCACGGCCTCCTCCCCACTCAGCCTGGGGGAGAGTCCAGGGTCGGCCTGTCCCCCTCCCCCCACTCCTGTCACTATCAGTCCCCTGTGCTCAGCTTACTGGCAGGGTTCCTCCTGGCTGTCCCCTCCTGCCTCCAGCGCTCTTTCCTCAGGTGTCCACGAGCTCGTCCTCATCCCTTTCTGGTCCTGCTCAGATGCCGCCTGAGGCTCCCAAAACAGGGTCTCACTCTGTCACCCAGGGTGGAGTGCAGTGGTGCAATCCAGCTCACTGCATCCTTGACCTCCCAGGCTCAAGCGACCTCTGCCAGCGAGCTTGTTCAATTCCTGCACCAACACGATACATTGTTCTAACTAGGCCCTACACACAGCAAAGATACATTGTACTTAAGTAAGGGCCCTGCACCAACACAGATACATTGTTACTAAGTAGAGGCCCTGCACCAACACACGATACACTTGTTACTAACTAAGGCCCTGCACCAACACAGATACGTTAGTTACTAAGTAAGGCCCTGCACCAACACAGATACGTTGTTACTAAATAAGGCCCTGTACCAACACAGATACATTGTTACTAACTAAGGCCCTGCACCAACACAGATACTTGTTACTAAGTAAGGCCCTGCTACCAACACAGATACATTGTTACTAACTAAGCCCTGCACCAGCCACAGATACATTGTTACTAAGGGCCCTACACCAGCACAGATACATTGTTACTAAGGCCCTGCACCAACACAGATACGTTGTTACTAAGTGAGCCCTGCACCAACACAGATACATTGTTACTAAGTAAGGCCCTGCACCAGCACGAGATACATTGTTACTAAGGCCCTACACCAGCACAGGATAACATTGTCTACTAACTAAGGCCCTGCACGCAACAAAGATACGTATGTTACTAAGTAAGGCCCTGCACCAACACAGATACATTGTTACTAACTAAGGCCCCATGCACCAACACAGATAATTGTTACAAGGCCCTGCACCAACACAGATACATTGTTACTAACTAAGGCCCTGCACCAACACAGATACGTTGTTACTAAGTAAGGCCCTTGCACCAACACAATACATTGTTACTAAATAAGGCCCTGTACCAACATTCAGATACATTGTTACTAACTAAGGCCTGCACCAACACAGATACGTTGTTACTAAGTAAGGCCCTGTACCAACCAGGACTACATTGTTACTAACTAAGGCCCTGCACCACACAGATCGTTGTTTACTAAGTAAGGCCCTGGCACCAACACAGATACATTGTTACTAACTAAGGCCCTGCACAACACAATACATTGTTACTAAGGCCCTGCACCAACACAGATACATTGTTACTAAGTAAGGCCCTGCACCAACACGATACATTGTTACTACTAAGGCCCTGCACCAACACAAATACGTTCTTACTAAGTAAGGCCCTGCACCAACACAGATACATTGTACTAACTAAGGCCCTGTACAACACAGATACGTTGTTACTAAGTAAGGCCCTGCACCAACACAGACTACATTGTTACTAGGCCCTGCACCAAACACAGATACATTGTACTAAGTAAGGCCCTGCAGCCAACTACCAGATGACATTGTTACTAACTAAGGCCCTGCACCAGCACAGATATATTGTTACTAAAGGCCCTGCACCAACCTACAGATACATTGTTACTAACTAAGGCCTGCACCAACAAGATATGTTGTTACTAAGGCCCTGCACTCACACAGATACATTGTTACTAAGTAAGGGCCCTGCACCAACACAGATACATTGTTACTAACTAAAGGCCCCTGCACCAACACAGATAGTTGTTACTAAGTATGGCCCTCACCAACACAGATACATTGTTACTAAGGCCCTGCACCAACACAGATACATGTTACTAAGTAAGGCCCTTGCACCAACACAGATACATTTGTTACTAACTAAGGCCCTGCACCAACACAGATACATTGTACTAAGGCCCTACACCGAGAATGGATACATTGTCACATGGTTACCTAAAGCCTTGCACCAACATGGACACATCATTACTAAACTAAGGCCCTGCACCAACACAGATACATTGTTACTAAGGCCCTGCACCAACACAGATACATTGTTACTAAGATAAGAGCCCTGCACCAACACAGAGTACATTGTTACTAACAAGGCCCTCACCAGCACAGATATATTGTTACTAAGGCCCTGACTCAACACAGATAATTGTTACTAACTCAAGGCCCTGCACCAACCAGATATGTTGTTACTAAGGCCCTGCACCCCAACACAGATACATTGTTACTAAGGCCCGCACCAACACAAGATACATTGTTACTAAGTAAGGCCCTGCACCAACACAGATACGTTGTTACTAACTAAGGCCCTGCACCAACACAGATACGTTGTTACTCGAAGTAAGGCCCTGCACCAATCACAATACATTGTTACTAAGGCCCTGCACCAAACAGATACATTGTTACTAAGTAAGGCCCTGCACCATACACAGATGCATTTTAGCTAACTAAGGCCCTGTACCAACACAGATACATTGTTACTAGCTAAGGCCCTGCACCAACACAAGATACATTGTTACTAAGTAAGGCCCTGTACCAACACAGATATATTGTTACTAACTAAGGCCCTGCACCAACACAGATACGGTTGTTACTAAGTAAGGC";
//	fprintf(stderr, "Ref:  %s\n", ref);
//	fprintf(stderr, "Read: %s\n", qry);
//
//	Align align;
//
//	align.pBuffer1 = new char[strlen(ref) * 4];
//	align.pBuffer2 = new char[strlen(ref) * 4];
//
//	int result = aligner->SingleAlign(0, 20, ref, qry, align, 0);
//	fprintf(stderr, "Alignment terminated with: %d\n", result);
//	fprintf(stderr, "Score: %f\n", align.Score);
//	fprintf(stderr, "Cigar: %s\n", align.pBuffer1);
//	fprintf(stderr, "MD: %s\n", align.pBuffer2);
//
//	delete aligner;
//	aligner = 0;
//
//	return 0;
//}

