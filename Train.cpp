/*******************************************************************************
* Copyright (c) 2015-2017
* School of Electrical, Computer and Energy Engineering, Arizona State University
* PI: Prof. Shimeng Yu
* All rights reserved.
*   
* This source code is part of NeuroSim - a device-circuit-algorithm framework to benchmark 
* neuro-inspired architectures with synaptic devices(e.g., SRAM and emerging non-volatile memory). 
* Copyright of the model is maintained by the developers, and the model is distributed under 
* the terms of the Creative Commons Attribution-NonCommercial 4.0 International Public License 
* http://creativecommons.org/licenses/by-nc/4.0/legalcode.
* The source code is free and you can redistribute and/or modify it
* by providing that the following conditions are met:
*   
*  1) Redistributions of source code must retain the above copyright notice,
*     this list of conditions and the following disclaimer. 
*   
*  2) Redistributions in binary form must reproduce the above copyright notice,
*     this list of conditions and the following disclaimer in the documentation
*     and/or other materials provided with the distribution.
*   
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
* 
* Developer list: 
*   Pai-Yu Chen     Email: pchen72 at asu dot edu 
*                     
*   Xiaochen Peng   Email: xpeng15 at asu dot edu
********************************************************************************/

#include <cstdio>
#include <iostream>
#include <vector>
#include <random>
#include <string>
#include <cmath>
#include "formula.h"
#include "Param.h"
#include "Array.h"
#include "Mapping.h"
#include "NeuroSim.h"

extern Param *param;

extern std::vector< std::vector<double> > Input;
extern std::vector< std::vector<int> > dInput;
extern std::vector< std::vector<double> > Output;

extern std::vector< std::vector<double> > weight1;
extern std::vector< std::vector<double> > weight2;
extern std::vector< std::vector<double> > deltaWeight1;
extern std::vector< std::vector<double> > deltaWeight2;
extern std::vector< std::vector<double> >  totalDeltaWeight1;
extern std::vector< std::vector<double> >  totalDeltaWeight1_abs;
extern std::vector< std::vector<double> >  totalDeltaWeight2;
extern std::vector< std::vector<double> >  totalDeltaWeight2_abs;

extern std::vector< std::vector<double> >  gradSquarePrev1;
extern std::vector< std::vector<double> >  gradSquarePrev2;
extern std::vector< std::vector<double> >  momentumPrev1;
extern std::vector< std::vector<double> >  momentumPrev2;
extern std::vector< std::vector<double> >  gradSum1;
extern std::vector< std::vector<double> >  gradSum2;


extern Technology techIH;
extern Technology techHO;
extern Array *arrayIH;
extern Array *arrayHO;
extern SubArray *subArrayIH;
extern SubArray *subArrayHO;
extern Adder adderIH;
extern Mux muxIH;
extern RowDecoder muxDecoderIH;
extern DFF dffIH;
extern Subtractor subtractorIH;
extern Adder adderHO;
extern Mux muxHO;
extern RowDecoder muxDecoderHO;
extern DFF dffHO;
extern Subtractor subtractorHO;

extern double totalWeightUpdate=0; // track the total weight update (absolute value) during the whole training process
extern double totalNumPulse=0;// track the total number of pulse for the weight update process; for Analog device only

/*Optimization functions*/
double gradt;
double GAMA=0.3;
double BETA1= 0.9, BETA2=0.9; 
double SGD(double gradient, double learning_rate);
double Momentum(double gradient, double learning_rate, double momentumPrev, double GAMA=0.3);
double Adagrad(double gradient, double learning_rate, double gradSquare, double EPSILON=1E-2);
double RMSprop(double gradient, double learning_rate, double gradSquarePrev,double GAMA=0.9, double EPSILON=1E-5);
double Adam(double gradient, double learning_rate, double momentumPreV, double velocityPrev, double epoch,double BETA1=0.9, double BETA2=0.9, double EPSILON=1E-5);
void WeightTransfer_2T1F(void);
void WeightTransfer(void);
void TransferEnergyLatencyCalculation(Array* array, SubArray* subArray);

void Train(const int numTrain, const int epochs, char *optimization_type) {
int numBatchReadSynapse;	    // # of read synapses in a batch read operation (decide later)
int numBatchWriteSynapse;	// # of write synapses in a batch write operation (decide later)
double outN1[param->nHide]; // Net input to the hidden layer [param->nHide]
double a1[param->nHide];    // Net output of hidden layer [param->nHide] also the input of hidden layer to output layer
                                // the value after the activation function
                                // also the input of hidden layer to output layer
int da1[param->nHide];  // Digitized net output of hidden layer [param->nHide] also the input of hidden layer to output layer
double outN2[param->nOutput];   // Net input to the output layer [param->nOutput]
double a2[param->nOutput];  // Net output of output layer [param->nOutput]

double s1[param->nHide];    // Output delta from input layer to the hidden layer [param->nHide]
double s2[param->nOutput];  // Output delta from hidden layer to the output layer [param->nOutput]

int train_batchsize = param -> numTrainImagesPerBatch;

	
	for (int t = 0; t < epochs; t++) {
		for (int batchSize = 0; batchSize < numTrain; batchSize++) {
			int i = rand() % param->numMnistTrainImages;  // Randomize sample
			/* First layer (input layer to the hidden layer) */
			std::fill_n(outN1, param->nHide, 0);
			std::fill_n(a1, param->nHide, 0);
        if (param->useHardwareInTrainingFF) {   // Hardware
				double sumArrayReadEnergy = 0;   // Use a temporary variable here since OpenMP does not support reduction on class member
                double readVoltage;
                double readVoltageMSB;  // for the hybrid cell
                double readPulseWidth;
                double readPulseWidthMSB;   // for the hybrid cell
           if(AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[0][0]))
           {
                 readVoltage = static_cast<eNVM*>(arrayIH->cell[0][0])->readVoltage;
				 readPulseWidth = static_cast<eNVM*>(arrayIH->cell[0][0])->readPulseWidth;
           }
           else if(HybridCell*temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0]))
           {
                readVoltage = static_cast<HybridCell*>(arrayIH->cell[0][0])->LSBcell.readVoltage;
				readPulseWidth = static_cast<HybridCell*>(arrayIH->cell[0][0])->LSBcell.readPulseWidth; 
                readVoltageMSB = static_cast<HybridCell*>(arrayIH->cell[0][0])->MSBcell_LTP.readVoltage;
				readPulseWidthMSB = static_cast<HybridCell*>(arrayIH->cell[0][0])->MSBcell_LTP.readPulseWidth;     
            }
            #pragma omp parallel for reduction(+: sumArrayReadEnergy)
				for (int j=0; j<param->nHide; j++) {
					if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[0][0])) {  // Analog eNVM
                        if (static_cast<eNVM*>(arrayIH->cell[0][0])->cmosAccess) {  // 1T1R
							sumArrayReadEnergy += arrayIH->wireGateCapRow * techIH.vdd * techIH.vdd * param->nInput; // All WLs open
						}
					} else if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayIH->cell[0][0])) { // Digital eNVM
						if (static_cast<eNVM*>(arrayIH->cell[0][0])->cmosAccess) {  // 1T1R
							sumArrayReadEnergy += arrayIH->wireGateCapRow * techIH.vdd * techIH.vdd; // Selected WL
						} else {    // Cross-point
							sumArrayReadEnergy += arrayIH->wireCapRow * techIH.vdd * techIH.vdd * (param->nInput - 1);  // Unselected WLs
						}
					} else if(HybridCell*temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0]))
                    {   // multiply with 3 because we need to read PCM_LTP, PCM_LTD and 3T1C cell
                        sumArrayReadEnergy += 3*(arrayIH->wireGateCapRow * techIH.vdd * techIH.vdd * param->nInput); // All WLs open
                    } 
                    
					for (int n=0; n<param->numBitInput; n++) {
						double pSumMaxAlgorithm = pow(2, n) / (param->numInputLevel - 1) * arrayIH->arrayRowSize;  // Max algorithm partial weighted sum for the nth vector bit (if both max input value and max weight are 1)
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[0][0])) {  // Analog eNVM
							double Isum = 0;    // weighted sum current
							double IsumMax = 0; // Max weighted sum current
                            double IsumMin = 0; 
							double inputSum = 0;    // Weighted sum current of input vector * weight=1 column
							for (int k=0; k<param->nInput; k++) {
								if ((dInput[i][k]>>n) & 1) {    // if the nth bit of dInput[i][k] is 1
									Isum += arrayIH->ReadCell(j,k);
                                    inputSum += arrayIH->GetMediumCellReadCurrent(j,k);    // get current of Dummy Column as reference
									sumArrayReadEnergy += arrayIH->wireCapRow * readVoltage * readVoltage; // Selected BLs (1T1R) or Selected WLs (cross-point)
								}
								IsumMax += arrayIH->GetMaxCellReadCurrent(j,k);
                                IsumMin += arrayIH->GetMinCellReadCurrent(j,k);
							}
							sumArrayReadEnergy += Isum * readVoltage * readPulseWidth;
							int outputDigits = (CurrentToDigits(Isum, IsumMax-IsumMin)-CurrentToDigits(inputSum, IsumMax-IsumMin));
                            //int outputDigits = (CurrentToDigits(Isum, IsumMax)-CurrentToDigits(inputSum, IsumMax)); 
                            outN1[j] += DigitsToAlgorithm(outputDigits, pSumMaxAlgorithm);
						}
                        else if(HybridCell*temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0]))
                        {
                            double Isum_LSB = 0;              // weighted sum current of the LTP cell
							double Isum_MSB_LTP = 0;    // weighted sum current of the LTP cell
							double Isum_MSB_LTD = 0;    // weighted sum current of the LTP cell
							double IsumMax_LSB = 0;            //the maximum weight sum current (all cells are at high conductance)
                            double IsumMax_MSB = 0; 
                            double IsumMin_LSB = 0;
                            double IsumMin_MSB = 0;
                            double inputSum_LSB= 0;      // Reference for LSB cell
							for (int k=0; k<param->nInput; k++) 
                            {
								if ((dInput[i][k]>>n) & 1) // if the nth bit of dInput[i][k] is 1
                                {    
									Isum_LSB += arrayIH->ReadCell(j,k,"LSB");                   // the weight sum of the Jth column
                                    Isum_MSB_LTP += arrayIH->ReadCell(j,k,"MSB_LTP");  
                                    Isum_MSB_LTD += arrayIH->ReadCell(j,k,"MSB_LTD");  
                                    inputSum_LSB += arrayIH->GetMediumCellReadCurrent(j,k);
									sumArrayReadEnergy += arrayIH->wireCapRow * readVoltage * readVoltage; //
									sumArrayReadEnergy += 2*arrayIH->wireCapRow * readVoltageMSB * readVoltageMSB; // 
								}
								IsumMax_LSB += arrayIH->GetMaxCellReadCurrent(j,k,"LSB");
								IsumMax_MSB += arrayIH->GetMaxCellReadCurrent(j,k,"MSB");
								IsumMin_LSB += arrayIH->GetMinCellReadCurrent(j,k,"LSB");
								IsumMin_MSB += arrayIH->GetMinCellReadCurrent(j,k,"MSB");
							}
                            sumArrayReadEnergy += Isum_LSB * readVoltage * readPulseWidth;
                            sumArrayReadEnergy += (Isum_MSB_LTP + Isum_MSB_LTD) * readVoltageMSB * readPulseWidthMSB;
                            int outputDigits;
						    int outputDigitsLSB = 2*(CurrentToDigits(Isum_LSB, IsumMax_LSB-IsumMin_LSB)-CurrentToDigits(inputSum_LSB, IsumMax_LSB-IsumMin_LSB)); //minus the reference
                            int outputDigitsMSB = (CurrentToDigits(Isum_MSB_LTP, IsumMax_MSB-IsumMin_MSB)-CurrentToDigits(Isum_MSB_LTD, IsumMax_MSB-IsumMin_MSB)); //minus the reference
                            outputDigits = static_cast<HybridCell*>(arrayIH->cell[0][0])->significance*outputDigitsMSB+outputDigitsLSB;
                            outN1[j] += DigitsToAlgorithm(outputDigits, pSumMaxAlgorithm)/(static_cast<HybridCell*>(arrayIH->cell[0][0])->significance+1);  
                        } 
                        else 
                        {    // SRAM or digital eNVM
                            bool digitalNVM = false; 
                            bool parallelRead = false;
                            if(DigitalNVM*temp = dynamic_cast<DigitalNVM*>(arrayIH->cell[0][0]))
                            {    digitalNVM = true;
                                if(static_cast<DigitalNVM*>(arrayIH->cell[0][0])->parallelRead == true) 
								{
                                    parallelRead = true;
                                }
                            }
                            if(digitalNVM && parallelRead) // parallel read-out for DigitalNVM
                            {
                                double Imax = static_cast<DigitalNVM*>(arrayIH->cell[0][0])->avgMaxConductance*static_cast<DigitalNVM*>(arrayIH->cell[0][0])->readVoltage;
                                double Imin = static_cast<DigitalNVM*>(arrayIH->cell[0][0])->avgMinConductance*static_cast<DigitalNVM*>(arrayIH->cell[0][0])->readVoltage;
                                double Isum = 0;    // weighted sum current
                                double IsumMax = 0; // Max weighted sum current
                                double inputSum = 0;    // Weighted sum current of input vector * weight=1 column
                                int Dsum=0;
                                int DsumMax = 0;
                                int Dref = 0;
                                for (int w=0;w<param->numWeightBit;w++){
                                    int colIndex = (j+1) * param->numWeightBit - (w+1);  // w=0 is the LSB
                                    for (int k=0; k<param->nInput; k++) 
                                    {
                                        if((dInput[i][k]>>n) & 1){ // accumulate the current along a column
                                            Isum += static_cast<DigitalNVM*>(arrayIH->cell[colIndex][k])->conductance*static_cast<DigitalNVM*>(arrayIH->cell[colIndex ][k])->readVoltage;
                                            //inputSum += Imin;
                                            // get the reference current
                                            inputSum += static_cast<DigitalNVM*>(arrayIH->cell[arrayIH->refColumnNumber][k])->conductance*static_cast<DigitalNVM*>(arrayIH->cell[arrayIH->refColumnNumber][k])->readVoltage;
                                        }
                                    }
                                    int outputDigits = (int) (Isum /(Imax-Imin)); // the output at the ADC of this column // basically, this is the number of "1" in this column
                                    int outputDigitsRef = (int) (inputSum/(Imax-Imin));
                                    outputDigits = outputDigits-outputDigitsRef;
                                        
                                    Dref = (int)(inputSum/Imin);
                                    Isum=0;
                                    inputSum=0;
                                    Dsum += outputDigits*(int) pow(2,w);  // get the weight represented by the column
                                    DsumMax += param->nInput*(int) pow(2,w); // the maximum weight that can be represented by this column
                                }
                                sumArrayReadEnergy += static_cast<DigitalNVM*>(arrayIH->cell[0][0])->readEnergy * arrayIH->numCellPerSynapse * arrayIH->arrayRowSize;
                                outN1[j] += (double)(Dsum - Dref*(pow(2,param->numWeightBit-1)-1)) / DsumMax * pSumMaxAlgorithm;
                            }
                            else
                            {	 // Digital NVM or SRAM row-by-row readout				
							    int Dsum = 0;
							    int DsumMax = 0;
							    int inputSum = 0;
							    for (int k=0; k<param->nInput; k++) {
								    if ((dInput[i][k]>>n) & 1) {    // if the nth bit of dInput[i][k] is 1
									    Dsum += (int)(arrayIH->ReadCell(j,k));
									    inputSum += pow(2, arrayIH->numCellPerSynapse-1) - 1;   // get the digital weights of the dummy column as reference
								    }
								    DsumMax += pow(2, arrayIH->numCellPerSynapse) - 1;
							    }
							    if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayIH->cell[0][0])) {    // Digital eNVM
								    sumArrayReadEnergy += static_cast<DigitalNVM*>(arrayIH->cell[0][0])->readEnergy * arrayIH->numCellPerSynapse * arrayIH->arrayRowSize;
							    } 
                                else {    // SRAM
								    sumArrayReadEnergy += static_cast<SRAM*>(arrayIH->cell[0][0])->readEnergy * arrayIH->numCellPerSynapse * arrayIH->arrayRowSize;
							    }
							    outN1[j] += (double)(Dsum - inputSum) / DsumMax * pSumMaxAlgorithm;
							}
						}
					}
					a1[j] = sigmoid(outN1[j]);
					da1[j] = round_th(a1[j]*(param->numInputLevel-1), param->Hthreshold);
				}
				arrayIH->readEnergy += sumArrayReadEnergy;

				numBatchReadSynapse = (int)ceil((double)param->nHide/param->numColMuxed);
				// Don't parallelize this loop since there may be update of member variables inside NeuroSim functions
				for (int j=0; j<param->nHide; j+=numBatchReadSynapse) {
					int numActiveRows = 0;  // Number of selected rows for NeuroSim
					for (int n=0; n<param->numBitInput; n++) {
						for (int k=0; k<param->nInput; k++) {
							if ((dInput[i][k]>>n) & 1) {    // if the nth bit of dInput[i][k] is 1
								numActiveRows++;
							}
						}
					}
					subArrayIH->activityRowRead = (double)numActiveRows/param->nInput/param->numBitInput;
					subArrayIH->readDynamicEnergy += NeuroSimSubArrayReadEnergy(subArrayIH);
					subArrayIH->readDynamicEnergy += NeuroSimNeuronReadEnergy(subArrayIH, adderIH, muxIH, muxDecoderIH, dffIH, subtractorIH);
					subArrayIH->readLatency += NeuroSimSubArrayReadLatency(subArrayIH);
					subArrayIH->readLatency += NeuroSimNeuronReadLatency(subArrayIH, adderIH, muxIH, muxDecoderIH, dffIH, subtractorIH);
				}
        } 
        else {    // Algorithm
				#pragma omp parallel for
				for (int j = 0; j < param->nHide; j++) {
					for (int k = 0; k < param->nInput; k++) {
						outN1[j] += Input[i][k] * weight1[j][k];
					}
					a1[j] = sigmoid(outN1[j]);
				}
        }

			/* Second layer (hidder layer to the output layer) */
			std::fill_n(outN2, param->nOutput, 0);
			std::fill_n(a2, param->nOutput, 0);
			if (param->useHardwareInTrainingFF) {   // Hardware
            double sumArrayReadEnergy = 0;  // Use a temporary variable here since OpenMP does not support reduction on class member
            double readVoltage;
            double readPulseWidth;
            double readVoltageMSB;
            double readPulseWidthMSB;
            if(AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[0][0])){
                readVoltage = static_cast<eNVM*>(arrayHO->cell[0][0])->readVoltage;
				readPulseWidth = static_cast<eNVM*>(arrayHO->cell[0][0])->readPulseWidth;
            }
            else if(HybridCell*temp = dynamic_cast<HybridCell*>(arrayHO->cell[0][0]))
            {
                readVoltage = static_cast<HybridCell*>(arrayHO->cell[0][0])->LSBcell.readVoltage;
				readPulseWidth = static_cast<HybridCell*>(arrayHO->cell[0][0])->LSBcell.readPulseWidth;
                readVoltageMSB = static_cast<HybridCell*>(arrayHO->cell[0][0])->MSBcell_LTP.readVoltage;
				readPulseWidthMSB = static_cast<HybridCell*>(arrayHO->cell[0][0])->MSBcell_LTP.readPulseWidth;             
            }

                #pragma omp parallel for reduction(+: sumArrayReadEnergy)
				for (int j=0; j<param->nOutput; j++) {
					if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[0][0])) {  // Analog eNVM
						if (static_cast<eNVM*>(arrayHO->cell[0][0])->cmosAccess) {  // 1T1R
							sumArrayReadEnergy += arrayHO->wireGateCapRow * techHO.vdd * techHO.vdd * param->nHide; // All WLs open
						}
					} else if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayHO->cell[0][0])) { // Digital eNVM
						if (static_cast<eNVM*>(arrayHO->cell[0][0])->cmosAccess) {  // 1T1R
							sumArrayReadEnergy += arrayHO->wireGateCapRow * techHO.vdd * techHO.vdd;    // Selected WL
						} else {    // Cross-point
							sumArrayReadEnergy += arrayHO->wireCapRow * techHO.vdd * techHO.vdd * (param->nHide - 1);   // Unselected WLs
						}
					}
                    else if(HybridCell*temp = dynamic_cast<HybridCell*>(arrayHO->cell[0][0]))
                    {   // multiply with 3 because we need to read PCM_LTP, PCM_LTD and 3T1C cell
                        sumArrayReadEnergy += 3*(arrayHO->wireGateCapRow * techIH.vdd * techIH.vdd * param->nInput); // All WLs open
                    } 
                    
					for (int n=0; n<param->numBitInput; n++) {
						double pSumMaxAlgorithm = pow(2, n) / (param->numInputLevel - 1) * arrayHO->arrayRowSize;    // Max algorithm partial weighted sum for the nth vector bit (if both max input value and max weight are 1)
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[0][0])) {  // Analog eNVM
							double Isum = 0;    // weighted sum current
							double IsumMax = 0; // Max weighted sum current
                            double IsumMin = 0; 
							double a1Sum = 0;    // Weighted sum current of input vector * weight=1 column                            
							for (int k=0; k<param->nHide; k++) {
								if ((da1[k]>>n) & 1) {    // if the nth bit of da1[k] is 1  
							 		Isum += arrayHO->ReadCell(j,k);
                                    a1Sum +=arrayHO->GetMediumCellReadCurrent(j,k);
                                    sumArrayReadEnergy += arrayHO->wireCapRow * readVoltage * readVoltage; // Selected BLs (1T1R) or Selected WLs (cross-point)								                                  
                                }
                                IsumMax += arrayHO->GetMaxCellReadCurrent(j,k);
                                IsumMin += arrayHO->GetMinCellReadCurrent(j,k);
							}
							sumArrayReadEnergy += Isum * readVoltage * readPulseWidth;
							int outputDigits = (CurrentToDigits(Isum, IsumMax-IsumMin)-CurrentToDigits(a1Sum, IsumMax-IsumMin)); //minus the reference
                            outN2[j] += DigitsToAlgorithm(outputDigits, pSumMaxAlgorithm);     
						} 
                        else if( HybridCell*temp = dynamic_cast<HybridCell*>(arrayHO->cell[0][0]))
                        {
                            double Isum_LSB = 0;              // weighted sum current of the LTP cell
							double Isum_MSB_LTP = 0;    // weighted sum current of the LTP cell
							double Isum_MSB_LTD = 0;    // weighted sum current of the LTP cell
							double IsumMax_LSB = 0;            //the maximum weight sum current (all cells are at high conductance)
                            double IsumMin_LSB = 0;
                            double IsumMax_MSB = 0;
                            double IsumMin_MSB = 0;
                            double a1Sum_LSB= 0;      // Reference for LSB cell
							for (int k=0; k<param->nHide; k++) {
								if ((da1[k]>>n) & 1) {    // if the nth bit of dInput[i][k] is 1
                                    Isum_LSB += arrayHO->ReadCell(j,k,"LSB");                   // the weight sum of the Jth column
                                    Isum_MSB_LTP += arrayHO->ReadCell(j,k,"MSB_LTP");  
                                    Isum_MSB_LTD += arrayHO->ReadCell(j,k,"MSB_LTD");  
                                    a1Sum_LSB += arrayHO->GetMediumCellReadCurrent(j,k);
									sumArrayReadEnergy += arrayHO->wireCapRow * readVoltage * readVoltage; // Selected BLs (1T1R) or Selected WLs (cross-point)
									sumArrayReadEnergy += 2*arrayHO->wireCapRow * readVoltageMSB * readVoltageMSB; // Selected BLs (1T1R) or Selected WLs (cross-point)
                                 }
                                 IsumMax_LSB += arrayHO->GetMaxCellReadCurrent(j,k,"LSB");
								 IsumMax_MSB += arrayHO->GetMaxCellReadCurrent(j,k,"MSB");
                                 IsumMin_LSB += arrayHO->GetMinCellReadCurrent(j,k,"LSB");
                                 IsumMin_MSB += arrayHO->GetMinCellReadCurrent(j,k,"MSB");
							}
							sumArrayReadEnergy += Isum_LSB * readVoltage * readPulseWidth;
                            sumArrayReadEnergy += (Isum_MSB_LTP + Isum_MSB_LTD) * readVoltageMSB * readPulseWidthMSB;
							int outputDigits;
                            int outputDigitsLSB = 2*(CurrentToDigits(Isum_LSB, IsumMax_LSB-IsumMin_LSB)-CurrentToDigits(a1Sum_LSB, IsumMax_LSB-IsumMin_LSB)); //minus the reference
                            //int outputDigitsLSB = CurrentToDigits(Isum_LSB, IsumMax_LSB-IsumMin_LSB)-CurrentToDigits(a1Sum_LSB, IsumMax_LSB-IsumMin_LSB); //minus the reference
                            int outputDigitsMSB = (CurrentToDigits(Isum_MSB_LTP, IsumMax_MSB-IsumMin_MSB)-CurrentToDigits(Isum_MSB_LTD, IsumMax_MSB-IsumMin_MSB)); //minus the reference
                            outputDigits = static_cast<HybridCell*>(arrayHO->cell[0][0])->significance*outputDigitsMSB+outputDigitsLSB;
                            outN2[j] += DigitsToAlgorithm(outputDigits, pSumMaxAlgorithm)/(static_cast<HybridCell*>(arrayIH->cell[0][0])->significance+1); 
                         } 
                        else 
                        {// SRAM or digital eNVM
                            bool digitalNVM = false; 
                            bool parallelRead = false;
                            if(DigitalNVM*temp = dynamic_cast<DigitalNVM*>(arrayHO->cell[0][0]))
                            {    digitalNVM = true;
                                if(static_cast<DigitalNVM*>(arrayHO->cell[0][0])->parallelRead == true) 
								{
                                    parallelRead = true;
                                }
                            }
                            if(digitalNVM && parallelRead)
                            {
                                double Imin = static_cast<DigitalNVM*>(arrayHO->cell[0][0])->avgMinConductance*static_cast<DigitalNVM*>(arrayHO->cell[0][0])->readVoltage;
                                double Imax = static_cast<DigitalNVM*>(arrayHO->cell[0][0])->avgMaxConductance*static_cast<DigitalNVM*>(arrayHO->cell[0][0])->readVoltage;
                                double Isum = 0;    // weighted sum current
                                double IsumMax = 0; // Max weighted sum current
                                double inputSum = 0;    // Weighted sum current of input vector * weight=1 column
                                int Dsum=0;
                                int DsumMax = 0;
                                int Dref = 0;
                                for (int w=0;w<param->numWeightBit;w++){
                                    int colIndex = (j+1) * param->numWeightBit - (w+1);  // w=0 is the LSB
                                    for (int k=0; k<param->nHide; k++) {
                                        if ((da1[k]>>n) & 1) { // accumulate the current along a column
                                            Isum += static_cast<DigitalNVM*>(arrayHO->cell[colIndex][k])->conductance*static_cast<DigitalNVM*>(arrayHO->cell[colIndex][k])->readVoltage;
                                            inputSum += static_cast<DigitalNVM*>(arrayHO->cell[arrayHO->refColumnNumber][k])->conductance*static_cast<DigitalNVM*>(arrayHO->cell[arrayHO->refColumnNumber][k])->readVoltage;                                            
                                            //inputSum += Imin;
                                        }
                                    }
                                    int outputDigits = (int) (Isum /(Imax-Imin)); // the output at the ADC of this column
                                    int outputDigitsRef = (int) (inputSum/(Imax-Imin)); // basically, this is the number of "1" in this column
                                    outputDigits = outputDigits-outputDigitsRef;
                                            
                                    Dref = (int)(inputSum/Imin);
                                    Isum=0;
                                    inputSum=0;
                                    Dsum += outputDigits*(int) pow(2,w);  // get the weight represented by the column
                                    DsumMax += param->nHide*(int) pow(2,w); // the maximum weight that can be represented by this column                                        
                                }
                                sumArrayReadEnergy += static_cast<DigitalNVM*>(arrayHO->cell[0][0])->readEnergy * arrayHO->numCellPerSynapse * arrayHO->arrayRowSize;
                                outN2[j] += (double)(Dsum - Dref*(pow(2,param->numWeightBit-1)-1)) / DsumMax * pSumMaxAlgorithm;
                            }
                            else
                            {                            
							    int Dsum = 0;
							    int DsumMax = 0;
							    int a1Sum = 0;
							    for (int k=0; k<param->nHide; k++) {
								    if ((da1[k]>>n) & 1) {    // if the nth bit of da1[k] is 1
									    Dsum += (int)(arrayHO->ReadCell(j,k));
									    a1Sum += pow(2, arrayHO->numCellPerSynapse-1) - 1;    // get current of Dummy Column as reference
								    }
								    DsumMax += pow(2, arrayHO->numCellPerSynapse) - 1;
							    } 
							    if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayHO->cell[0][0])) {    // Digital eNVM
								    sumArrayReadEnergy += static_cast<DigitalNVM*>(arrayHO->cell[0][0])->readEnergy * arrayHO->numCellPerSynapse * arrayHO->arrayRowSize;
							    } 
                                else {
								    sumArrayReadEnergy += static_cast<SRAM*>(arrayHO->cell[0][0])->readEnergy * arrayHO->numCellPerSynapse * arrayHO->arrayRowSize;
							    }
							    outN2[j] += (double)(Dsum - a1Sum) / DsumMax * pSumMaxAlgorithm;
                            }
						}
					}
					a2[j] = sigmoid(outN2[j]);
				}
				arrayHO->readEnergy += sumArrayReadEnergy;
				numBatchReadSynapse = (int)ceil((double)param->nOutput/param->numColMuxed);
				// Don't parallelize this loop since there may be update of member variables inside NeuroSim functions
				for (int j=0; j<param->nOutput; j+=numBatchReadSynapse) {
					int numActiveRows = 0;  // Number of selected rows for NeuroSim
					for (int n=0; n<param->numBitInput; n++) {
						for (int k=0; k<param->nHide; k++) {
							if ((da1[k]>>n) & 1) {    // if the nth bit of da1[k] is 1
								numActiveRows++;
							}
						}
					}
					subArrayHO->activityRowRead = (double)numActiveRows/param->nHide/param->numBitInput;
					subArrayHO->readDynamicEnergy += NeuroSimSubArrayReadEnergy(subArrayHO);
					subArrayHO->readDynamicEnergy += NeuroSimNeuronReadEnergy(subArrayHO, adderHO, muxHO, muxDecoderHO, dffHO, subtractorHO);
					subArrayHO->readLatency += NeuroSimSubArrayReadLatency(subArrayHO);
					subArrayHO->readLatency += NeuroSimNeuronReadLatency(subArrayHO, adderHO, muxHO, muxDecoderHO, dffHO, subtractorHO);
				}
			} else {
				#pragma omp parallel for
				for (int j = 0; j < param->nOutput; j++) {
					for (int k = 0; k < param->nHide; k++) {
						outN2[j] += a1[k] * weight2[j][k];
					}
					a2[j] = sigmoid(outN2[j]);
				}
			}

			// Backpropagation
			/* Second layer (hidden layer to the output layer) */
			for (int j = 0; j < param->nOutput; j++){
                s2[j] = -2*a2[j] * (1 - a2[j])*(Output[i][j] - a2[j]);
			}

			/* First layer (input layer to the hidden layer) */
			std::fill_n(s1, param->nHide, 0);
			#pragma omp parallel for
			for (int j = 0; j < param->nHide; j++) {
				for (int k = 0; k < param->nOutput; k++) {
					s1[j] += a1[j] * (1 - a1[j]) * weight2[k][j] * s2[k];
				}
			}

			// Weight update
			/* Update weight of the first layer (input layer to the hidden layer) */
			if (param->useHardwareInTrainingWU) {
				double sumArrayWriteEnergy = 0;   // Use a temporary variable here since OpenMP does not support reduction on class member
				double sumNeuroSimWriteEnergy = 0;   // Use a temporary variable here since OpenMP does not support reduction on class member
				double sumWriteLatencyAnalogNVM = 0;   // Use a temporary variable here since OpenMP does not support reduction on class member
				double numWriteOperation = 0;	// Average number of write batches in the whole array. Use a temporary variable here since OpenMP does not support reduction on class member
                double writeVoltageLTP = static_cast<eNVM*>(arrayIH->cell[0][0])->writeVoltageLTP;
                double writeVoltageLTD = static_cast<eNVM*>(arrayIH->cell[0][0])->writeVoltageLTD;
                double writePulseWidthLTP = static_cast<eNVM*>(arrayIH->cell[0][0])->writePulseWidthLTP;
                double writePulseWidthLTD = static_cast<eNVM*>(arrayIH->cell[0][0])->writePulseWidthLTD;
                if(eNVM *temp = dynamic_cast<eNVM*>(arrayIH->cell[0][0])){
                    writeVoltageLTP = static_cast<eNVM*>(arrayIH->cell[0][0])->writeVoltageLTP;
                    writeVoltageLTD = static_cast<eNVM*>(arrayIH->cell[0][0])->writeVoltageLTD;
				    writePulseWidthLTP = static_cast<eNVM*>(arrayIH->cell[0][0])->writePulseWidthLTP;
				    writePulseWidthLTD = static_cast<eNVM*>(arrayIH->cell[0][0])->writePulseWidthLTD;
                }
                else if(HybridCell *temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0])){
                     writeVoltageLTP = static_cast<HybridCell*>(arrayIH->cell[0][0])->LSBcell.writeVoltageLTP;
                     writeVoltageLTD = static_cast<HybridCell*>(arrayIH->cell[0][0])->LSBcell.writeVoltageLTD;
                     writePulseWidthLTP = static_cast<HybridCell*>(arrayIH->cell[0][0])->LSBcell.writePulseWidthLTP;
                    writePulseWidthLTD = static_cast<HybridCell*>(arrayIH->cell[0][0])->LSBcell.writePulseWidthLTD;               
                }
                numBatchWriteSynapse = (int)ceil((double)arrayIH->arrayColSize / param->numWriteColMuxed);
				#pragma omp parallel for reduction(+: sumArrayWriteEnergy, sumNeuroSimWriteEnergy, sumWriteLatencyAnalogNVM)
				for (int k = 0; k < param->nInput; k++) {
					int numWriteOperationPerRow = 0;	// Number of write batches in a row that have any weight change
					int numWriteCellPerOperation = 0;	// Average number of write cells per batch in a row (for digital eNVM)
					for (int j = 0; j < param->nHide; j+=numBatchWriteSynapse) {
						/* Batch write */
						int start = j;
						int end = j + numBatchWriteSynapse - 1;
						if (end >= param->nHide) {
							end = param->nHide - 1;
						}
						double maxLatencyLTP = 0;	// Max latency for AnalogNVM's LTP or weight increase in this batch write
						double maxLatencyLTD = 0;	// Max latency for AnalogNVM's LTD or weight decrease in this batch write
						bool weightChangeBatch = false;	// Specify if there is any weight change in the entire write batch
                        
                        double maxWeightUpdated=0;
                        double maxPulseNum =0;
                        double actualWeightUpdated;
                        for (int jj = start; jj <= end; jj++) { // Selected cells
                            /*can support multiple optimization algorithm*/
                            gradt = s1[jj] * Input[i][k];
                            gradSum1[jj][k] += gradt; // sum over the gradient over all the training samples in this batch
                            if (std::string(optimization_type) == "SGD"){
                                deltaWeight1[jj][k] = SGD(gradt, param->alpha1);                        
                            }   
                            else if((batchSize+1) % train_batchsize == 0){ // batch based algorithms
                                // get the batch gradient
                                gradSum1[jj][k] /= train_batchsize;
                                if (std::string(optimization_type)=="Momentum")
                                {
                                    gradSum1[jj][k] *= train_batchsize;
                                    deltaWeight1[jj][k] = Momentum(gradSum1[jj][k], param->alpha1,momentumPrev1[jj][k]);
                                    momentumPrev1[jj][k] = GAMA*momentumPrev1[jj][k]+(1-GAMA)*gradSum1[jj][k];
                                }
                                else if(std::string(optimization_type)=="RMSprop")
                                {
                                    deltaWeight1[jj][k] = RMSprop(gradSum1[jj][k], param->alpha1, gradSquarePrev1[jj][k]);                                
                                    gradSquarePrev1[jj][k] = GAMA*gradSquarePrev1[jj][k]+(1-GAMA)*pow(gradSum1[jj][k], 2);   
                                }
                                else if(std::string(optimization_type) == "Adam")
                                {
                                    deltaWeight1[jj][k] = Adam(gradSum1[jj][k], param->alpha1, momentumPrev1[jj][k], gradSquarePrev1[jj][k],(batchSize+1)/train_batchsize);
                                    momentumPrev1[jj][k] = BETA1*momentumPrev1[jj][k]+(1-BETA1)*gradSum1[jj][k];
                                    gradSquarePrev1[jj][k] = BETA2*gradSquarePrev1[jj][k]+(1-BETA2)*pow(gradSum1[jj][k], 2);
                                }
                                else std::cout<<"please specify an optimization method" <<end;
                                gradSum1[jj][k] = 0;
                            }
                    
                           /* tracking code */
                            totalDeltaWeight1[jj][k] += deltaWeight1[jj][k];
                            totalDeltaWeight1_abs[jj][k] += fabs(deltaWeight1[jj][k]);

                            // find the actual weight update
                            if(deltaWeight1[jj][k]+weight1[jj][k] > param-> maxWeight)
                            {
                                actualWeightUpdated=param->maxWeight - weight1[jj][k];    
                            }
                            else if(deltaWeight1[jj][k]+weight1[jj][k] < param->minWeight)
                            {
                                actualWeightUpdated=param->minWeight - weight1[jj][k];
                            } 
                            else actualWeightUpdated=deltaWeight1[jj][k];
                            
                            if(fabs(actualWeightUpdated)>maxWeightUpdated)
                            {
                                maxWeightUpdated =fabs(actualWeightUpdated);
                            }
                            
                            if(std::string(optimization_type) == "SGD" || (batchSize+1) % train_batchsize == 0 ){
                                if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[jj][k])) {	// Analog eNVM
                                    arrayIH->WriteCell(jj, k, deltaWeight1[jj][k], weight1[jj][k], param->maxWeight, param->minWeight, true);
                                    weight1[jj][k] = arrayIH->ConductanceToWeight(jj, k, param->maxWeight, param->minWeight); 
                                    weightChangeBatch = weightChangeBatch || static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse;
                                    if(fabs(static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse) > maxPulseNum)
                                    {
                                        maxPulseNum=fabs(static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse);
                                    }
                                    /* Get maxLatencyLTP and maxLatencyLTD */
                                    if (static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeLatencyLTP > maxLatencyLTP)
                                        maxLatencyLTP = static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeLatencyLTP;
                                    if (static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeLatencyLTD > maxLatencyLTD)
                                        maxLatencyLTD = static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeLatencyLTD;
                                }							
                                else if (HybridCell *temp = dynamic_cast<HybridCell*>(arrayIH->cell[jj][k])) {	// Analog eNVM
                                    arrayIH->WriteCell(jj, k, deltaWeight1[jj][k], weight1[jj][k], param->maxWeight, param->minWeight, true);
                                    weight1[jj][k] = arrayIH->ConductanceToWeight(jj, k, param->maxWeight, param->minWeight);
                                    weightChangeBatch = weightChangeBatch || static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.numPulse;
                                    if(fabs(static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.numPulse) > maxPulseNum)
                                    {
                                        maxPulseNum=fabs(static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.numPulse);
                                    }
                                    /* Get maxLatencyLTP and maxLatencyLTD */
                                    if (static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.writeLatencyLTP > maxLatencyLTP)
                                        maxLatencyLTP = static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.writeLatencyLTP;
                                    if (static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.writeLatencyLTD > maxLatencyLTD)
                                        maxLatencyLTD = static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.writeLatencyLTD;
                                } 
                                else {	// SRAM and digital eNVM
                                    weight1[jj][k] = weight1[jj][k] + deltaWeight1[jj][k];
                                    arrayIH->WriteCell(jj, k, deltaWeight1[jj][k], weight1[jj][k], param->maxWeight, param->minWeight, true);
                                    weightChangeBatch = weightChangeBatch || arrayIH->weightChange[jj][k];
                                }
                            }
							
						}
                        // update the track variables
                        totalWeightUpdate += maxWeightUpdated;
                        totalNumPulse += maxPulseNum;
                        
						numWriteOperationPerRow += weightChangeBatch;
						for (int jj = start; jj <= end; jj++) { // Selected cells
							if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[0][0])) {  // Analog eNVM
								/* Set the max latency for all the selected cells in this batch */
								static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeLatencyLTP = maxLatencyLTP;
								static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeLatencyLTD = maxLatencyLTD;
								if (param->writeEnergyReport && weightChangeBatch) {
									if (static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->nonIdenticalPulse) {	// Non-identical write pulse scheme
										if (static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse > 0) {	// LTP
											static_cast<eNVM*>(arrayIH->cell[jj][k])->writeVoltageLTP = sqrt(static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeVoltageSquareSum / static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse);	// RMS value of LTP write voltage
											static_cast<eNVM*>(arrayIH->cell[jj][k])->writeVoltageLTD = static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->VinitLTD + 0.5 * static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->VstepLTD * static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->maxNumLevelLTD;	// Use average voltage of LTD write voltage
										} else if (static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse < 0) {	// LTD
											static_cast<eNVM*>(arrayIH->cell[jj][k])->writeVoltageLTP = static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->VinitLTP + 0.5 * static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->VstepLTP * static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->maxNumLevelLTP;    // Use average voltage of LTP write voltage
											static_cast<eNVM*>(arrayIH->cell[jj][k])->writeVoltageLTD = sqrt(static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeVoltageSquareSum / (-1*static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse));    // RMS value of LTD write voltage
										} else {	// Half-selected during LTP and LTD phases
											static_cast<eNVM*>(arrayIH->cell[jj][k])->writeVoltageLTP = static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->VinitLTP + 0.5 * static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->VstepLTP * static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->maxNumLevelLTP;    // Use average voltage of LTP write voltage
											static_cast<eNVM*>(arrayIH->cell[jj][k])->writeVoltageLTD = static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->VinitLTD + 0.5 * static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->VstepLTD * static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->maxNumLevelLTD;    // Use average voltage of LTD write voltage
										}
									}
									static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->WriteEnergyCalculation(arrayIH->wireCapCol);
									sumArrayWriteEnergy += static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->writeEnergy; 
                                    // add the transfer energy if this is a 2T1F cell
                                    // the transfer energy will be 0 if there is no transfer
                                    if(_2T1F* temp = dynamic_cast<_2T1F*>(arrayIH->cell[jj][k]))
                                        sumArrayWriteEnergy += static_cast<_2T1F*>(arrayIH->cell[jj][k])->transWriteEnergy;
								}
							} 
                            else if(HybridCell *temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0]))
                            {
 								/* Set the max latency for all the selected cells in this batch */
								static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.writeLatencyLTP = maxLatencyLTP;
								static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.writeLatencyLTD = maxLatencyLTD;
								if (param->writeEnergyReport && weightChangeBatch) {
                                    // need to modifiy the code for non-identical pulse
									static_cast<HybridCell*>(arrayIH->cell[jj][k])->WriteEnergyCalculation(arrayIH->wireCapCol);
									sumArrayWriteEnergy += static_cast<HybridCell*>(arrayIH->cell[jj][k])->writeEnergy;
								}                               
                            }
                            else if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayIH->cell[0][0])) { // Digital eNVM
								if (param->writeEnergyReport && arrayIH->weightChange[jj][k]) {
									for (int n=0; n<arrayIH->numCellPerSynapse; n++) {  // n=0 is LSB
										sumArrayWriteEnergy += static_cast<DigitalNVM*>(arrayIH->cell[(jj+1) * arrayIH->numCellPerSynapse - (n+1)][k])->writeEnergy;
										int bitPrev = static_cast<DigitalNVM*>(arrayIH->cell[(jj+1) * arrayIH->numCellPerSynapse - (n+1)][k])->bitPrev;
										int bit = static_cast<DigitalNVM*>(arrayIH->cell[(jj+1) * arrayIH->numCellPerSynapse - (n+1)][k])->bit;
										if (bit != bitPrev) {
											numWriteCellPerOperation += 1;
										}
									}
								}
							} else {    // SRAM
								if (param->writeEnergyReport && arrayIH->weightChange[jj][k]) {
									sumArrayWriteEnergy += static_cast<SRAM*>(arrayIH->cell[jj * arrayIH->numCellPerSynapse][k])->writeEnergy;
								}
							}
						}
                        
						/* Latency for each batch write in Analog eNVM */
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[0][0])) {	// Analog eNVM
							sumWriteLatencyAnalogNVM += maxLatencyLTP + maxLatencyLTD;
						}
                        else if(HybridCell *temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0])){ // HybridCell
 							sumWriteLatencyAnalogNVM += maxLatencyLTP + maxLatencyLTD;
                        }
						/* Energy consumption on array caps for eNVM */
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[0][0])) {  // Analog eNVM
							if (param->writeEnergyReport && weightChangeBatch) {
								if (static_cast<AnalogNVM*>(arrayIH->cell[0][0])->nonIdenticalPulse) { // Non-identical write pulse scheme
									writeVoltageLTP = static_cast<AnalogNVM*>(arrayIH->cell[0][0])->VinitLTP + 0.5 * static_cast<AnalogNVM*>(arrayIH->cell[0][0])->VstepLTP * static_cast<AnalogNVM*>(arrayIH->cell[0][0])->maxNumLevelLTP;    // Use average voltage of LTP write voltage
									writeVoltageLTD = static_cast<AnalogNVM*>(arrayIH->cell[0][0])->VinitLTD + 0.5 * static_cast<AnalogNVM*>(arrayIH->cell[0][0])->VstepLTD * static_cast<AnalogNVM*>(arrayIH->cell[0][0])->maxNumLevelLTD;    // Use average voltage of LTD write voltage
								}
								if (static_cast<eNVM*>(arrayIH->cell[0][0])->cmosAccess) {  // 1T1R
									// The energy on selected SLs is included in WriteCell()
									sumArrayWriteEnergy += arrayIH->wireGateCapRow * techIH.vdd * techIH.vdd * 2;   // Selected WL (*2 means both LTP and LTD phases)
									sumArrayWriteEnergy += arrayIH->wireCapRow * writeVoltageLTP * writeVoltageLTP;   // Selected BL (LTP phases)
									sumArrayWriteEnergy += arrayIH->wireCapCol * writeVoltageLTP * writeVoltageLTP * (param->nHide-numBatchWriteSynapse);   // Unselected SLs (LTP phase)
									// No LTD part because all unselected rows and columns are V=0
								} else {
									sumArrayWriteEnergy += arrayIH->wireCapRow * writeVoltageLTP * writeVoltageLTP;    // Selected WL (LTP phase)
									sumArrayWriteEnergy += arrayIH->wireCapRow * writeVoltageLTP/2 * writeVoltageLTP/2 * (param->nInput - 1);  // Unselected WLs (LTP phase)
									sumArrayWriteEnergy += arrayIH->wireCapCol * writeVoltageLTP/2 * writeVoltageLTP/2 * (param->nHide - numBatchWriteSynapse);   // Unselected BLs (LTP phase)
									sumArrayWriteEnergy += arrayIH->wireCapRow * writeVoltageLTD/2 * writeVoltageLTD/2 * (param->nInput - 1);    // Unselected WLs (LTD phase)
									sumArrayWriteEnergy += arrayIH->wireCapCol * writeVoltageLTD/2 * writeVoltageLTD/2 * (param->nHide - numBatchWriteSynapse); // Unselected BLs (LTD phase)
								}
							}
						}
						else if (HybridCell *temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0])) {  // Hybridcell
							if (param->writeEnergyReport && weightChangeBatch) {
									// The energy on selected SLs is included in WriteCell()
									sumArrayWriteEnergy += arrayIH->wireGateCapRow * techIH.vdd * techIH.vdd * 2;   // Selected WL (*2 means both LTP and LTD phases)
									sumArrayWriteEnergy += arrayIH->wireCapRow * writeVoltageLTP * writeVoltageLTP;   // Selected BL (LTP phases)
									sumArrayWriteEnergy += arrayIH->wireCapCol * writeVoltageLTP * writeVoltageLTP * (param->nHide-numBatchWriteSynapse);   // Unselected SLs (LTP phase)
									// No LTD part because all unselected rows and columns are V=0
                            }
                        }
                        else if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayIH->cell[0][0])) { // Digital eNVM
							if (param->writeEnergyReport && weightChangeBatch) {
								if (static_cast<eNVM*>(arrayIH->cell[0][0])->cmosAccess) {  // 1T1R
									// The energy on selected columns is included in WriteCell()
									sumArrayWriteEnergy += arrayIH->wireGateCapRow * techIH.vdd * techIH.vdd * 2;   // Selected WL (*2 for both SET and RESET phases)
								} else {    // Cross-point
									sumArrayWriteEnergy += arrayIH->wireCapRow * writeVoltageLTP * writeVoltageLTP;   // Selected WL (SET phase)
									sumArrayWriteEnergy += arrayIH->wireCapRow * writeVoltageLTP/2 * writeVoltageLTP/2 * (param->nInput - 1);    // Unselected WLs (SET phase)
									sumArrayWriteEnergy += arrayIH->wireCapCol * writeVoltageLTP/2 * writeVoltageLTP/2 * (param->nHide - numBatchWriteSynapse) * arrayIH->numCellPerSynapse;   // Unselected BLs (SET phase)
									sumArrayWriteEnergy += arrayIH->wireCapRow * writeVoltageLTD/2 * writeVoltageLTD/2 * (param->nInput - 1);   // Unselected WLs (RESET phase)
									sumArrayWriteEnergy += arrayIH->wireCapCol * writeVoltageLTD/2 * writeVoltageLTD/2 * (param->nHide - numBatchWriteSynapse) * arrayIH->numCellPerSynapse;   // Unselected BLs (RESET phase)
								}
							}
						}
						/* Half-selected cells for eNVM */
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[0][0])) {  // Analog eNVM
							if (!static_cast<eNVM*>(arrayIH->cell[0][0])->cmosAccess && param->writeEnergyReport) { // Cross-point
								for (int jj = 0; jj < param->nHide; jj++) { // Half-selected cells in the same row
									if (jj >= start && jj <= end) { continue; } // Skip the selected cells
									sumArrayWriteEnergy += (writeVoltageLTP/2 * writeVoltageLTP/2 * static_cast<eNVM*>(arrayIH->cell[jj][k])->conductanceAtHalfVwLTP * maxLatencyLTP + writeVoltageLTD/2 * writeVoltageLTD/2 * static_cast<eNVM*>(arrayIH->cell[jj][k])->conductanceAtHalfVwLTD * maxLatencyLTD);
								}
								for (int kk = 0; kk < param->nInput; kk++) {    // Half-selected cells in other rows
									// Note that here is a bit inaccurate if using OpenMP, because the weight on other rows (threads) are also being updated
									if (kk == k) { continue; } // Skip the selected row
									for (int jj = start; jj <= end; jj++) {
										sumArrayWriteEnergy += (writeVoltageLTP/2 * writeVoltageLTP/2 * static_cast<eNVM*>(arrayIH->cell[jj][kk])->conductanceAtHalfVwLTP * maxLatencyLTP + writeVoltageLTD/2 * writeVoltageLTD/2 * static_cast<eNVM*>(arrayIH->cell[jj][kk])->conductanceAtHalfVwLTD * maxLatencyLTD);
									}
								}
							}
						} else if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayIH->cell[0][0])) { // Digital eNVM
							if (!static_cast<eNVM*>(arrayIH->cell[0][0])->cmosAccess && param->writeEnergyReport && weightChangeBatch) { // Cross-point
								for (int jj = 0; jj < param->nHide; jj++) {    // Half-selected synapses in the same row
									if (jj >= start && jj <= end) { continue; } // Skip the selected synapses
									for (int n=0; n<arrayIH->numCellPerSynapse; n++) {  // n=0 is LSB
										int colIndex = (jj+1) * arrayIH->numCellPerSynapse - (n+1);
										sumArrayWriteEnergy += writeVoltageLTP/2 * writeVoltageLTP/2 * static_cast<eNVM*>(arrayIH->cell[colIndex][k])->conductanceAtHalfVwLTP * maxLatencyLTP + writeVoltageLTD/2 * writeVoltageLTD/2 * static_cast<eNVM*>(arrayIH->cell[colIndex][k])->conductanceAtHalfVwLTD * maxLatencyLTD;
									}
								}
								for (int kk = 0; kk < param->nInput; kk++) {   // Half-selected synapses in other rows
									// Note that here is a bit inaccurate if using OpenMP, because the weight on other rows (threads) are also being updated
									if (kk == k) { continue; } // Skip the selected row
									for (int jj = start; jj <= end; jj++) {
										for (int n=0; n<arrayIH->numCellPerSynapse; n++) {  // n=0 is LSB
											int colIndex = (jj+1) * arrayIH->numCellPerSynapse - (n+1);
											sumArrayWriteEnergy += writeVoltageLTP/2 * writeVoltageLTP/2 * static_cast<eNVM*>(arrayIH->cell[colIndex][kk])->conductanceAtHalfVwLTP * maxLatencyLTP + writeVoltageLTD/2 * writeVoltageLTD/2 * static_cast<eNVM*>(arrayIH->cell[colIndex][kk])->conductanceAtHalfVwLTD * maxLatencyLTD;
										}
									}
								}
							}
						}
					}
					/* Calculate the average number of write pulses on the selected row */
					#pragma omp critical    // Use critical here since NeuroSim class functions may update its member variables
					{
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayIH->cell[0][0])) {  // Analog eNVM
							int sumNumWritePulse = 0;
							for (int j = 0; j < param->nHide; j++) {
								sumNumWritePulse += abs(static_cast<AnalogNVM*>(arrayIH->cell[j][k])->numPulse);    // Note that LTD has negative pulse number
							}
							subArrayIH->numWritePulse = sumNumWritePulse / param->nHide;
							double writeVoltageSquareSumRow = 0;
							if (param->writeEnergyReport) {
								if (static_cast<AnalogNVM*>(arrayIH->cell[0][0])->nonIdenticalPulse) { // Non-identical write pulse scheme
									for (int j = 0; j < param->nHide; j++) {
										writeVoltageSquareSumRow += static_cast<AnalogNVM*>(arrayIH->cell[j][k])->writeVoltageSquareSum;
									}
									if (sumNumWritePulse > 0) {	// Prevent division by 0
										subArrayIH->cell.writeVoltage = sqrt(writeVoltageSquareSumRow / sumNumWritePulse);	// RMS value of write voltage in a row
									} else {
										subArrayIH->cell.writeVoltage = 0;
									}
								}
							}
						}
                        else if(HybridCell *temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0]))
                        {
							int sumNumWritePulse = 0;
							for (int j = 0; j < param->nHide; j++) {
								sumNumWritePulse += abs(static_cast<HybridCell*>(arrayIH->cell[j][k])->LSBcell.numPulse);    // Note that LTD has negative pulse number
							}
							subArrayIH->numWritePulse = sumNumWritePulse / param->nHide;
                        }
						numWriteCellPerOperation = (double)numWriteCellPerOperation/numWriteOperationPerRow;
						sumNeuroSimWriteEnergy += NeuroSimSubArrayWriteEnergy(subArrayIH, numWriteOperationPerRow, numWriteCellPerOperation);

					}
					numWriteOperation += numWriteOperationPerRow;
                    sumNeuroSimWriteEnergy += NeuroSimSubArrayWriteEnergy(subArrayIH, numWriteOperationPerRow, numWriteCellPerOperation);
				}
				if(!std::isnan(sumArrayWriteEnergy)){
    				arrayIH->writeEnergy += sumArrayWriteEnergy;
				}				
				subArrayIH->writeDynamicEnergy += sumNeuroSimWriteEnergy;
				numWriteOperation = numWriteOperation / param->nInput;
				subArrayIH->writeLatency += NeuroSimSubArrayWriteLatency(subArrayIH, numWriteOperation, sumWriteLatencyAnalogNVM);
			} else {
				#pragma omp parallel for
				for (int j = 0; j < param->nHide; j++) {
					for (int k = 0; k < param->nInput; k++) {
						deltaWeight1[j][k] = - param->alpha1 * s1[j] * Input[i][k];
						weight1[j][k] = weight1[j][k] + deltaWeight1[j][k];
						if (weight1[j][k] > param->maxWeight) {
							deltaWeight1[j][k] -= weight1[j][k] - param->maxWeight;
							weight1[j][k] = param->maxWeight;
						} else if (weight1[j][k] < param->minWeight) {
							deltaWeight1[j][k] += param->minWeight - weight1[j][k];
							weight1[j][k] = param->minWeight;
						}
						if (param->useHardwareInTrainingFF) {
							arrayIH->WriteCell(j, k, deltaWeight1[j][k], weight1[j][k], param->maxWeight, param->minWeight, false);
						}
					}
				}
			}

			/* Update weight of the second layer (hidden layer to the output layer) */
			if (param->useHardwareInTrainingWU) {
				double sumArrayWriteEnergy = 0;   // Use a temporary variable here since OpenMP does not support reduction on class member
				double sumNeuroSimWriteEnergy = 0;   // Use a temporary variable here since OpenMP does not support reduction on class member
				double sumWriteLatencyAnalogNVM = 0;	// Use a temporary variable here since OpenMP does not support reduction on class member
				double numWriteOperation = 0;	// Average number of write batches in the whole array. Use a temporary variable here since OpenMP does not support reduction on class member
                double writeVoltageLTP;
                double writeVoltageLTD;
                double writePulseWidthLTP;
                double writePulseWidthLTD;				
                if(eNVM *temp = dynamic_cast<eNVM*>(arrayHO->cell[0][0])){
                     writeVoltageLTP = static_cast<eNVM*>(arrayHO->cell[0][0])->writeVoltageLTP;
				     writeVoltageLTD = static_cast<eNVM*>(arrayHO->cell[0][0])->writeVoltageLTD;
				     writePulseWidthLTP = static_cast<eNVM*>(arrayHO->cell[0][0])->writePulseWidthLTP;
				     writePulseWidthLTD = static_cast<eNVM*>(arrayHO->cell[0][0])->writePulseWidthLTD;
                }
                else if(HybridCell *temp = dynamic_cast<HybridCell*>(arrayHO->cell[0][0])){
                     writeVoltageLTP = static_cast<HybridCell*>(arrayHO->cell[0][0])->LSBcell.writeVoltageLTP;
				     writeVoltageLTD = static_cast<HybridCell*>(arrayHO->cell[0][0])->LSBcell.writeVoltageLTD;
				     writePulseWidthLTP = static_cast<HybridCell*>(arrayHO->cell[0][0])->LSBcell.writePulseWidthLTP;
				     writePulseWidthLTD = static_cast<HybridCell*>(arrayHO->cell[0][0])->LSBcell.writePulseWidthLTD;               
                }
				numBatchWriteSynapse = (int)ceil((double)arrayHO->arrayColSize / param->numWriteColMuxed);
				#pragma omp parallel for reduction(+: sumArrayWriteEnergy, sumNeuroSimWriteEnergy, sumWriteLatencyAnalogNVM)
				for (int k = 0; k < param->nHide; k++) {
					int numWriteOperationPerRow = 0;    // Number of write batches in a row that have any weight change
					int numWriteCellPerOperation = 0;   // Average number of write cells per batch in a row (for digital eNVM)
					for (int j = 0; j < param->nOutput; j+=numBatchWriteSynapse) {
						/* Batch write */
						int start = j;
						int end = j + numBatchWriteSynapse - 1;
						if (end >= param->nOutput) {
							end = param->nOutput - 1;
						}
						double maxLatencyLTP = 0;   // Max latency for AnalogNVM's LTP or weight increase in this batch write
						double maxLatencyLTD = 0;   // Max latency for AnalogNVM's LTD or weight decrease in this batch write
						bool weightChangeBatch = false; // Specify if there is any weight change in the entire write batch
                        
                        double maxWeightUpdated=0;
                        double maxPulseNum =0;
                        double actualWeightUpdated=0;
                        for (int jj = start; jj <= end; jj++) { // Selected cells

							// deltaWeight2[jj][k] = -param->alpha2 * s2[jj] * a1[k];
                            gradt = s2[jj] * a1[k];
                            gradSum2[jj][k] += gradt; // sum over the gradient over all the training samples in this batch
                         if (std::string(optimization_type) == "SGD") 
                            deltaWeight2[jj][k] = SGD(gradt, param->alpha2); 
                         else if((batchSize+1) % train_batchsize == 0){
                            gradSum2[jj][k] /= train_batchsize;
                            if(std::string(optimization_type)=="Momentum")
                            {
                                gradSum2[jj][k] *= train_batchsize;
                                deltaWeight2[jj][k] = Momentum(gradSum2[jj][k], param->alpha2,momentumPrev2[jj][k]);
                                momentumPrev2[jj][k] = GAMA*momentumPrev2[jj][k]+(1-GAMA)*gradSum2[jj][k];
                            }
                            else if(std::string(optimization_type)=="RMSprop")
                            {
                                deltaWeight2[jj][k] = RMSprop(gradSum2[jj][k], param->alpha2, gradSquarePrev2[jj][k]);
                                gradSquarePrev2[jj][k] = GAMA*gradSquarePrev2[jj][k]+(1-GAMA)*pow(gradSum2[jj][k], 2);
                            }
                            else if(std::string(optimization_type) == "Adam")
                            {
                                deltaWeight2[jj][k] = Adam(gradSum2[jj][k], param->alpha2, momentumPrev2[jj][k], gradSquarePrev2[jj][k],(batchSize+1)/train_batchsize);
                                momentumPrev2[jj][k] = BETA1*momentumPrev2[jj][k]+(1-BETA1)*gradSum2[jj][k];
                                gradSquarePrev2[jj][k] = BETA2*gradSquarePrev2[jj][k]+(1-BETA2)*pow(gradSum2[jj][k], 2);
                            }
                            else std::cout<<"please specify an optimization method" <<end;
                            gradSum2[jj][k] = 0;
                        }
                            /*tracking code*/
                            totalDeltaWeight2[jj][k] += deltaWeight2[jj][k];
                            totalDeltaWeight2_abs[jj][k] += fabs(deltaWeight2[jj][k]);
                          
                            /* track the number of weight update*/
                            // find the actual weight update
                            if(deltaWeight2[jj][k]+weight2[jj][k] > param-> maxWeight)
                            {
                                actualWeightUpdated=param->maxWeight - weight2[jj][k];    
                            }
                            else if(deltaWeight2[jj][k]+weight2[jj][k] < param->minWeight)
                            {
                                actualWeightUpdated=param->minWeight - weight2[jj][k];
                            } 
                            else actualWeightUpdated=deltaWeight2[jj][k];
                            
                            if(fabs(actualWeightUpdated)>maxWeightUpdated)
                            {
                                maxWeightUpdated =fabs(actualWeightUpdated);
                            }		
                        if(std::string(optimization_type) == "SGD" || (batchSize+1) % train_batchsize == 0){
							if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[jj][k])) { // Analog eNVM
                                arrayHO->WriteCell(jj, k, deltaWeight2[jj][k], weight2[jj][k], param->maxWeight, param->minWeight, true);
							    weight2[jj][k] = arrayHO->ConductanceToWeight(jj, k, param->maxWeight, param->minWeight);
								weightChangeBatch = weightChangeBatch || static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->numPulse;
                                if(fabs(static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse) > maxPulseNum)
                                {
                                    maxPulseNum=fabs(static_cast<AnalogNVM*>(arrayIH->cell[jj][k])->numPulse);
                                }
                                /* Get maxLatencyLTP and maxLatencyLTD */
								if (static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->writeLatencyLTP > maxLatencyLTP)
									maxLatencyLTP = static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->writeLatencyLTP;
								if (static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->writeLatencyLTD > maxLatencyLTD)
									maxLatencyLTD = static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->writeLatencyLTD;
							}
                            else if (HybridCell *temp = dynamic_cast<HybridCell*>(arrayHO->cell[jj][k])) {	// Analog eNVM
                                arrayHO->WriteCell(jj, k, deltaWeight2[jj][k], weight2[jj][k], param->maxWeight, param->minWeight, true);
                                weight2[jj][k] = arrayHO->ConductanceToWeight(jj, k, param->maxWeight, param->minWeight);
                                weightChangeBatch = weightChangeBatch || static_cast<HybridCell*>(arrayHO->cell[jj][k])->LSBcell.numPulse;
                                if(fabs(static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.numPulse) > maxPulseNum)
                                {
                                    maxPulseNum=fabs(static_cast<HybridCell*>(arrayIH->cell[jj][k])->LSBcell.numPulse);
                                }
                                /* Get maxLatencyLTP and maxLatencyLTD */
								if (static_cast<HybridCell*>(arrayHO->cell[jj][k])->LSBcell.writeLatencyLTP > maxLatencyLTP)
									maxLatencyLTP = static_cast<HybridCell*>(arrayHO->cell[jj][k])->LSBcell.writeLatencyLTP;
								if (static_cast<HybridCell*>(arrayHO->cell[jj][k])->LSBcell.writeLatencyLTD > maxLatencyLTD)
									maxLatencyLTD = static_cast<HybridCell*>(arrayHO->cell[jj][k])->LSBcell.writeLatencyLTD;
							} 
                            else {    // SRAM and digital eNVM
								weight2[jj][k] = weight2[jj][k] + deltaWeight2[jj][k];
								arrayHO->WriteCell(jj, k, deltaWeight2[jj][k], weight2[jj][k], param->maxWeight, param->minWeight, true);
								weightChangeBatch = weightChangeBatch || arrayHO->weightChange[jj][k];
							}
							
						}
                        }
                        totalWeightUpdate += maxWeightUpdated;
                        totalNumPulse += maxPulseNum;
                        
                        /* Latency for each batch write in Analog eNVM */
						numWriteOperationPerRow += weightChangeBatch;
						for (int jj = start; jj <= end; jj++) { // Selected cells
							if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[0][0])) {  // Analog eNVM
								/* Set the max latency for all the cells in this batch */
								static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->writeLatencyLTP = maxLatencyLTP;
								static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->writeLatencyLTD = maxLatencyLTD;
								if (param->writeEnergyReport && weightChangeBatch) {
									if (static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->nonIdenticalPulse) { // Non-identical write pulse scheme
										if (static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->numPulse > 0) {  // LTP
											static_cast<eNVM*>(arrayHO->cell[jj][k])->writeVoltageLTP = sqrt(static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->writeVoltageSquareSum / static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->numPulse);   // RMS value of LTP write voltage
											static_cast<eNVM*>(arrayHO->cell[jj][k])->writeVoltageLTD = static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->VinitLTD + 0.5 * static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->VstepLTD * static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->maxNumLevelLTD;    // Use average voltage of LTD write voltage
										} else if (static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->numPulse < 0) {    // LTD
											static_cast<eNVM*>(arrayHO->cell[jj][k])->writeVoltageLTP = static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->VinitLTP + 0.5 * static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->VstepLTP * static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->maxNumLevelLTP;    // Use average voltage of LTP write voltage
											static_cast<eNVM*>(arrayHO->cell[jj][k])->writeVoltageLTD = sqrt(static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->writeVoltageSquareSum / (-1*static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->numPulse));    // RMS value of LTD write voltage
										} else {	// Half-selected during LTP and LTD phases
											static_cast<eNVM*>(arrayHO->cell[jj][k])->writeVoltageLTP = static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->VinitLTP + 0.5 * static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->VstepLTP * static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->maxNumLevelLTP;    // Use average voltage of LTP write voltage
											static_cast<eNVM*>(arrayHO->cell[jj][k])->writeVoltageLTD = static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->VinitLTD + 0.5 * static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->VstepLTD * static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->maxNumLevelLTD;    // Use average voltage of LTD write voltage
										}
									}
									static_cast<AnalogNVM*>(arrayHO->cell[jj][k])->WriteEnergyCalculation(arrayHO->wireCapCol);
									sumArrayWriteEnergy += static_cast<eNVM*>(arrayHO->cell[jj][k])->writeEnergy;
                                    if(_2T1F* temp = dynamic_cast<_2T1F*>(arrayHO->cell[jj][k]))
                                        sumArrayWriteEnergy += static_cast<_2T1F*>(arrayHO->cell[jj][k])->transWriteEnergy;
								}
							}
                            else if(HybridCell *temp = dynamic_cast<HybridCell*>(arrayHO->cell[0][0]))
                            {
 								/* Set the max latency for all the selected cells in this batch */
								static_cast<HybridCell*>(arrayHO->cell[jj][k])->LSBcell.writeLatencyLTP = maxLatencyLTP;
								static_cast<HybridCell*>(arrayHO->cell[jj][k])->LSBcell.writeLatencyLTD = maxLatencyLTD;
								if (param->writeEnergyReport && weightChangeBatch) {
                                    // need to modifiy the code for non-identical pulse
									static_cast<HybridCell*>(arrayHO->cell[jj][k])->WriteEnergyCalculation(arrayIH->wireCapCol);
									sumArrayWriteEnergy += static_cast<HybridCell*>(arrayHO->cell[jj][k])->writeEnergy;
								}                               
                            } 
                            else if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayHO->cell[0][0])) { // Digital eNVM
								if (param->writeEnergyReport && arrayHO->weightChange[jj][k]) {
									for (int n=0; n<arrayHO->numCellPerSynapse; n++) {  // n=0 is LSB
										sumArrayWriteEnergy += static_cast<DigitalNVM*>(arrayHO->cell[(jj+1) * arrayHO->numCellPerSynapse - (n+1)][k])->writeEnergy;
										int bitPrev = static_cast<DigitalNVM*>(arrayHO->cell[(jj+1) * arrayHO->numCellPerSynapse - (n+1)][k])->bitPrev;
										int bit = static_cast<DigitalNVM*>(arrayHO->cell[(jj+1) * arrayHO->numCellPerSynapse - (n+1)][k])->bit;
										if (bit != bitPrev) {
											numWriteCellPerOperation += 1;
										}
									}
								}
							} else {    // SRAM
								if (param->writeEnergyReport && arrayHO->weightChange[jj][k]) {
									sumArrayWriteEnergy += static_cast<SRAM*>(arrayHO->cell[jj * arrayHO->numCellPerSynapse][k])->writeEnergy;
								}
							}
						}
						/* Latency for each batch write in Analog eNVM */
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[0][0])) {  // Analog eNVM
							sumWriteLatencyAnalogNVM += maxLatencyLTP + maxLatencyLTD;
						}
                        else if(HybridCell *temp = dynamic_cast<HybridCell*>(arrayIH->cell[0][0])){ // HybridCell
 							sumWriteLatencyAnalogNVM += maxLatencyLTP + maxLatencyLTD;
                        }
						/* Energy consumption on array caps for eNVM */
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[0][0])) {  // Analog eNVM
							if (param->writeEnergyReport && weightChangeBatch) {
								if (static_cast<AnalogNVM*>(arrayHO->cell[0][0])->nonIdenticalPulse) { // Non-identical write pulse scheme
									writeVoltageLTP = static_cast<AnalogNVM*>(arrayHO->cell[0][0])->VinitLTP + 0.5 * static_cast<AnalogNVM*>(arrayHO->cell[0][0])->VstepLTP * static_cast<AnalogNVM*>(arrayHO->cell[0][0])->maxNumLevelLTP;    // Use average voltage of LTP write voltage
									writeVoltageLTD = static_cast<AnalogNVM*>(arrayHO->cell[0][0])->VinitLTD + 0.5 * static_cast<AnalogNVM*>(arrayHO->cell[0][0])->VstepLTD * static_cast<AnalogNVM*>(arrayHO->cell[0][0])->maxNumLevelLTD;    // Use average voltage of LTD write voltage
								}
								if (static_cast<eNVM*>(arrayHO->cell[0][0])->cmosAccess) {  // 1T1R
									// The energy on selected SLs is included in WriteCell()
									sumArrayWriteEnergy += arrayHO->wireGateCapRow * techHO.vdd * techHO.vdd * 2;   // Selected WL (*2 means both LTP and LTD phases)
									sumArrayWriteEnergy += arrayHO->wireCapRow * writeVoltageLTP * writeVoltageLTP;   // Selected BL (LTP phases)
									sumArrayWriteEnergy += arrayHO->wireCapCol * writeVoltageLTP * writeVoltageLTP * (param->nOutput-numBatchWriteSynapse);   // Unselected SLs (LTP phase)
									// No LTD part because all unselected rows and columns are V=0
								} else {
									sumArrayWriteEnergy += arrayHO->wireCapRow * writeVoltageLTP * writeVoltageLTP;   // Selected WL (LTP phase)
									sumArrayWriteEnergy += arrayHO->wireCapRow * writeVoltageLTP/2 * writeVoltageLTP/2 * (param->nHide - 1);    // Unselected WLs (LTP phase)
									sumArrayWriteEnergy += arrayHO->wireCapCol * writeVoltageLTP/2 * writeVoltageLTP/2 * (param->nOutput - numBatchWriteSynapse); // Unselected BLs (LTP phase)
									sumArrayWriteEnergy += arrayHO->wireCapRow * writeVoltageLTD/2 * writeVoltageLTD/2 * (param->nHide - 1);    // Unselected WLs (LTD phase)
									sumArrayWriteEnergy += arrayHO->wireCapCol * writeVoltageLTD/2 * writeVoltageLTD/2 * (param->nOutput - numBatchWriteSynapse); // Unselected BLs (LTD phase)
								}
							}
						}
						else if (HybridCell *temp = dynamic_cast<HybridCell*>(arrayHO->cell[0][0])) {  // Hybridcell
							if (param->writeEnergyReport && weightChangeBatch) {
									// The energy on selected SLs is included in WriteCell()
									sumArrayWriteEnergy += arrayHO->wireGateCapRow * techIH.vdd * techIH.vdd * 2;   // Selected WL (*2 means both LTP and LTD phases)
									sumArrayWriteEnergy += arrayHO->wireCapRow * writeVoltageLTP * writeVoltageLTP;   // Selected BL (LTP phases)
									sumArrayWriteEnergy += arrayHO->wireCapCol * writeVoltageLTP * writeVoltageLTP * (param->nHide-numBatchWriteSynapse);   // Unselected SLs (LTP phase)
									// No LTD part because all unselected rows and columns are V=0
                            }
                        } 
                        else if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayHO->cell[0][0])) { // Digital eNVM
							if (param->writeEnergyReport && weightChangeBatch) {
								if (static_cast<eNVM*>(arrayHO->cell[0][0])->cmosAccess) {  // 1T1R
									// The energy on selected columns is included in WriteCell()
									sumArrayWriteEnergy += arrayHO->wireGateCapRow * techHO.vdd * techHO.vdd * 2;   // Selected WL (*2 for both SET and RESET phases)
								} else {    // Cross-point
									sumArrayWriteEnergy += arrayHO->wireCapRow * writeVoltageLTP * writeVoltageLTP;   // Selected WL (SET phase)
									sumArrayWriteEnergy += arrayHO->wireCapRow * writeVoltageLTP/2 * writeVoltageLTP/2 * (param->nInput - 1);    // Unselected WLs (SET phase)
									sumArrayWriteEnergy += arrayHO->wireCapCol * writeVoltageLTP/2 * writeVoltageLTP/2 * (param->nHide - numBatchWriteSynapse) * arrayHO->numCellPerSynapse;  // Unselected BLs (SET phase)
									sumArrayWriteEnergy += arrayHO->wireCapRow * writeVoltageLTD/2 * writeVoltageLTD/2 * (param->nInput - 1);  // Unselected WLs (RESET phase)
									sumArrayWriteEnergy += arrayHO->wireCapCol * writeVoltageLTD/2 * writeVoltageLTD/2 * (param->nHide - numBatchWriteSynapse) * arrayHO->numCellPerSynapse;  // Unselected BLs (RESET phase)
								}
							}
						}
						/* Half-selected cells for eNVM */
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[0][0])) {  // Analog eNVM
							if (!static_cast<eNVM*>(arrayHO->cell[0][0])->cmosAccess && param->writeEnergyReport) { // Cross-point
								for (int jj = 0; jj < param->nOutput; jj++) {    // Half-selected cells in the same row
									if (jj >= start && jj <= end) { continue; } // Skip the selected cells
									sumArrayWriteEnergy += (writeVoltageLTP/2 * writeVoltageLTP/2 * static_cast<eNVM*>(arrayHO->cell[jj][k])->conductanceAtHalfVwLTP * maxLatencyLTP + writeVoltageLTD/2 * writeVoltageLTD/2 * static_cast<eNVM*>(arrayHO->cell[jj][k])->conductanceAtHalfVwLTD * maxLatencyLTD);
								}
								for (int kk = 0; kk < param->nHide; kk++) { // Half-selected cells in other rows
									// Note that here is a bit inaccurate if using OpenMP, because the weight on other rows (threads) are also being updated
									if (kk == k) { continue; }  // Skip the selected row
									for (int jj = start; jj <= end; jj++) {
										sumArrayWriteEnergy += (writeVoltageLTP/2 * writeVoltageLTP/2 * static_cast<eNVM*>(arrayHO->cell[jj][kk])->conductanceAtHalfVwLTP * maxLatencyLTP + writeVoltageLTD/2 * writeVoltageLTD/2 * static_cast<eNVM*>(arrayHO->cell[jj][kk])->conductanceAtHalfVwLTD * maxLatencyLTD);
									}
								}
							}
						} else if (DigitalNVM *temp = dynamic_cast<DigitalNVM*>(arrayHO->cell[0][0])) { // Digital eNVM
							if (!static_cast<eNVM*>(arrayHO->cell[0][0])->cmosAccess && param->writeEnergyReport && weightChangeBatch) { // Cross-point
								for (int jj = 0; jj < param->nOutput; jj++) {    // Half-selected synapses in the same row
									if (jj >= start && jj <= end) { continue; } // Skip the selected synapses
									for (int n=0; n<arrayHO->numCellPerSynapse; n++) {  // n=0 is LSB
										int colIndex = (jj+1) * arrayHO->numCellPerSynapse - (n+1);
										sumArrayWriteEnergy += writeVoltageLTP/2 * writeVoltageLTP/2 * static_cast<eNVM*>(arrayHO->cell[colIndex][k])->conductanceAtHalfVwLTP * maxLatencyLTP + writeVoltageLTD/2 * writeVoltageLTD/2 * static_cast<eNVM*>(arrayHO->cell[colIndex][k])->conductanceAtHalfVwLTD * maxLatencyLTD;
									}
								}
								for (int kk = 0; kk < param->nHide; kk++) {    // Half-selected synapses in other rows
									// Note that here is a bit inaccurate if using OpenMP, because the weight on other rows (threads) are also being updated
									if (kk == k) { continue; }  // Skip the selected row
									for (int jj = start; jj <= end; jj++) {
										for (int n=0; n<arrayHO->numCellPerSynapse; n++) {  // n=0 is LSB
											int colIndex = (jj+1) * arrayHO->numCellPerSynapse - (n+1);
											sumArrayWriteEnergy += writeVoltageLTP/2 * writeVoltageLTP/2 * static_cast<eNVM*>(arrayHO->cell[colIndex][kk])->conductanceAtHalfVwLTP * maxLatencyLTP + writeVoltageLTD/2 * writeVoltageLTD/2 * static_cast<eNVM*>(arrayHO->cell[colIndex][kk])->conductanceAtHalfVwLTD * maxLatencyLTD;
										}
									}
								}
							}
						}
					}
					/* Calculate the average number of write pulses on the selected row */
					#pragma omp critical    // Use critical here since NeuroSim class functions may update its member variables
					{
						if (AnalogNVM *temp = dynamic_cast<AnalogNVM*>(arrayHO->cell[0][0])) {  // Analog eNVM
							int sumNumWritePulse = 0;
							for (int j = 0; j < param->nOutput; j++) {
								sumNumWritePulse += abs(static_cast<AnalogNVM*>(arrayHO->cell[j][k])->numPulse);    // Note that LTD has negative pulse number
							}
							subArrayHO->numWritePulse = sumNumWritePulse / param->nOutput;
							double writeVoltageSquareSumRow = 0;
							if (param->writeEnergyReport) {
								if (static_cast<AnalogNVM*>(arrayHO->cell[0][0])->nonIdenticalPulse) { // Non-identical write pulse scheme
									for (int j = 0; j < param->nOutput; j++) {
										writeVoltageSquareSumRow += static_cast<AnalogNVM*>(arrayHO->cell[j][k])->writeVoltageSquareSum;
									}
									if (sumNumWritePulse > 0) {	// Prevent division by 0
										subArrayHO->cell.writeVoltage = sqrt(writeVoltageSquareSumRow / sumNumWritePulse);  // RMS value of write voltage in a row
									} else {
										subArrayHO->cell.writeVoltage = 0;
									}
								}
                                else if(HybridCell *temp = dynamic_cast<HybridCell*>(arrayHO->cell[0][0]))
                                {
							         int sumNumWritePulse = 0;
							         for (int j = 0; j < param->nHide; j++) {
								           sumNumWritePulse += abs(static_cast<HybridCell*>(arrayHO->cell[j][k])->LSBcell.numPulse);    // Note that LTD has negative pulse number
							          }
                                     subArrayHO->numWritePulse = sumNumWritePulse / param->nHide;
                                }
							}
						}
						numWriteCellPerOperation = (double)numWriteCellPerOperation/numWriteOperationPerRow;
						sumNeuroSimWriteEnergy += NeuroSimSubArrayWriteEnergy(subArrayHO, numWriteOperationPerRow, numWriteCellPerOperation);
					}
					numWriteOperation += numWriteOperationPerRow;
				}
				arrayHO->writeEnergy += sumArrayWriteEnergy;
				subArrayHO->writeDynamicEnergy += sumNeuroSimWriteEnergy;
				numWriteOperation = numWriteOperation / param->nHide;
				subArrayHO->writeLatency += NeuroSimSubArrayWriteLatency(subArrayHO, numWriteOperation, sumWriteLatencyAnalogNVM);
			} else {
				#pragma omp parallel for
				for (int j = 0; j < param->nOutput; j++) {
					for (int k = 0; k < param->nHide; k++) {
						deltaWeight2[j][k] = -param->alpha2 * s2[j] * a1[k];
						weight2[j][k] = weight2[j][k] + deltaWeight2[j][k];
						if (weight2[j][k] > param->maxWeight) {
							deltaWeight2[j][k] -= weight2[j][k] - param->maxWeight;
							weight2[j][k] = param->maxWeight;
						} else if (weight2[j][k] < param->minWeight) {
							deltaWeight2[j][k] += param->minWeight - weight2[j][k];
							weight2[j][k] = param->minWeight;
						}
						if (param->useHardwareInTrainingFF) {
							arrayHO->WriteCell(j, k, deltaWeight2[j][k], weight2[j][k], param->maxWeight, param->minWeight, false);
						}
					}
				}
			}
		}
    }
}

double SGD(double gradient, double learning_rate){
    return -learning_rate * gradient; 
}

double Momentum(double gradient, double learning_rate, double momentumPrev, double GAMA){
    double momentumNow; 
    momentumNow = GAMA*momentumPrev + (1-GAMA)*gradient;
    return -learning_rate*momentumNow;
}

double Adagrad(double gradient, double learning_rate, double gradSquare, double EPSILON){
    return -learning_rate/(sqrt(gradSquare)+EPSILON)*gradient;
}

double RMSprop(double gradient, double learning_rate, double gradSquarePrev,double GAMA, double EPSILON){
    double gradSquareNow;
    gradSquareNow = GAMA*gradSquarePrev+(1-GAMA)*pow(gradient,2);
    return -learning_rate/(sqrt(gradSquareNow)+EPSILON)*gradient;
}

double Adam(double gradient, double learning_rate, double momentumPrev, double velocityPrev, double epoch,double BETA1, double BETA2, double EPSILON){
    double mt = BETA1*momentumPrev+(1-BETA1)*gradient;
    double vt = BETA2*velocityPrev+(1-BETA2)*pow(gradient,2);
    // correct the bias
    mt = mt/(1-pow(BETA1,epoch));
    vt = vt/(1-pow(BETA2,epoch));
    return -learning_rate*mt/(sqrt(vt)+EPSILON);
}

void WeightTransfer_2T1F(void)
{
        for(int i=0; i<param->nInput;i++){
            int rowLTP=0; // if the row programmed MSB to higher level
            int rowLTD=0;
            for (int j=0; j<param->nHide; j++) {
                static_cast<_2T1F*>(arrayIH->cell[j][i])->WeightTransfer( );
                arrayIH->transferEnergy += static_cast<_2T1F*>(arrayIH->cell[j][i])->transEnergy;
                if(static_cast<_2T1F*>(arrayIH->cell[j][i])->transLTP)
                    rowLTP=1;
                else if(static_cast<_2T1F*>(arrayIH->cell[j][i])->transLTD)
                    rowLTD=1;
            }
            subArrayIH->transferLatency += (rowLTP+rowLTD)*static_cast<_2T1F*>(arrayIH->cell[0][0])->transPulseWidth;
        }
        for(int i=0; i<param->nHide;i++){
            int rowLTP=0; // if the row programmed MSB to higher level
            int rowLTD=0;
            for (int j=0; j<param->nOutput; j++) {
                static_cast<_2T1F*>(arrayHO->cell[j][i])->WeightTransfer( );
                arrayHO->transferEnergy += static_cast<_2T1F*>(arrayHO->cell[j][i])->transEnergy;
                if(static_cast<_2T1F*>(arrayHO->cell[j][i])->transLTP)
                    rowLTP=1;
                else if(static_cast<_2T1F*>(arrayHO->cell[j][i])->transLTD)
                    rowLTD=1;
            }
            subArrayHO->transferLatency += (rowLTP+rowLTD)*static_cast<_2T1F*>(arrayIH->cell[0][0])->transPulseWidth;
        }
}

void WeightTransfer(void) // WeightTransfer for the Hybridcell
{
    for(int i=0; i<param->nInput;i++){
        for (int j=0; j<param->nHide; j++) {
            // transfer the weight from MSB to LSB
            double weightMSB_LTP = arrayIH->ConductanceToWeight(j,i,param->maxWeight, param->minWeight, "MSB_LTP");
            double weightMSB_LTD = arrayIH->ConductanceToWeight(j,i,param->maxWeight, param->minWeight, "MSB_LTD"); 
            double weight_cell = arrayIH->ConductanceToWeight(j,i,param->maxWeight, param->minWeight);
            double weight_LSB = arrayIH->ConductanceToWeight(j,i,param->maxWeight, param->minWeight,"LSB");       
            static_cast<HybridCell*>(arrayIH->cell[j][i])->WeightTransfer(weightMSB_LTP, weightMSB_LTD, param->minWeight, param->maxWeight, arrayIH->wireCapCol);
            weightMSB_LTP = arrayIH->ConductanceToWeight(j,i,param->maxWeight, param->minWeight, "MSB_LTP");
            weightMSB_LTD = arrayIH->ConductanceToWeight(j,i,param->maxWeight, param->minWeight, "MSB_LTD"); 
            weight_cell = arrayIH->ConductanceToWeight(j,i,param->maxWeight, param->minWeight);
            weight_LSB = arrayIH->ConductanceToWeight(j,i,param->maxWeight, param->minWeight,"LSB");
        }
    }
    TransferEnergyLatencyCalculation(arrayIH, subArrayIH);
    
    for(int i=0; i<param->nHide;i++){
        for (int j=0; j<param->nOutput; j++) {
           double weightMSB_LTP = arrayHO->ConductanceToWeight( j, i, param->maxWeight, param->minWeight, "MSB_LTP");
           double weightMSB_LTD = arrayHO->ConductanceToWeight( j, i, param->maxWeight, param->minWeight, "MSB_LTD");            
           static_cast<HybridCell*>(arrayHO->cell[j][i])->WeightTransfer(weightMSB_LTP, weightMSB_LTD, param->minWeight, param->maxWeight, arrayHO->wireCapCol);
        }
    }
    TransferEnergyLatencyCalculation(arrayHO, subArrayHO);
} 

void TransferEnergyLatencyCalculation(Array* array, SubArray* subArray){
    
// read energy calculation

double readVoltage = static_cast<HybridCell*>(array->cell[0][0])->LSBcell.readVoltage;
double readPulseWidth = static_cast<HybridCell*>(array->cell[0][0])->LSBcell.readPulseWidth;
// the energy consumption when charging the row to read
array->transferReadEnergy += subArray->numRow*array->wireCapRow * readVoltage * readVoltage;

// all the rows are active
// read it row-by-row
subArray->activityRowRead = subArray->numRow/param->nInput;
subArray->transferReadDynamicEnergy += NeuroSimSubArrayReadEnergy(subArray);
subArray->transferReadLatency += subArray->numRow*NeuroSimSubArrayReadLatency(subArray);
// energy consumption when turning on the word line
array->transferReadEnergy += subArray->numRow*array->wireGateCapRow * techIH.vdd * techIH.vdd; 


// calculate the write latency
for (int i=0; i<subArray->numRow;i++){ // iterate over the row

     double maxLatencyLTP = 0;
     double maxLatencyLTD = 0;
     int sumNumWritePulse = 0;
     int numWriteCellPerOperation = 0;
     int numWriteOperationPerRow = 0;
     double writeVoltageSquareSumRow = 0;

    for (int j=0; j<subArray->numCol;j++){ // iterate over the column
        array->transferReadEnergy+= static_cast<HybridCell*>(array->cell[j][i])->transferReadEnergy;
        array->transferWriteEnergy+= static_cast<HybridCell*>(array->cell[j][i])->transferWriteEnergy;
        // get the maxLatency of each row 
        if (static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTP.writeLatencyLTP > maxLatencyLTP)
            maxLatencyLTP = static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTP.writeLatencyLTP;     
        if (static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTD.writeLatencyLTP > maxLatencyLTD)  // the conductance of both LTP and LTD cell is increased
            maxLatencyLTD = static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTD.writeLatencyLTP;
        // get the number of write per row;
        // for each PCM pair, at most one operation. 
        if(static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTP.numPulse!=0 || static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTD.numPulse!=0)
            numWriteOperationPerRow++;
        // calculate the write energy
        // only one of them can be none zero
        sumNumWritePulse += abs(static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTP.numPulse);    // Note that LTD has negative pulse number
        sumNumWritePulse += abs(static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTD.numPulse);    // Note that LTD has negative pulse number
        writeVoltageSquareSumRow += static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTP.writeVoltageSquareSum;
        writeVoltageSquareSumRow += static_cast<HybridCell*>(array->cell[j][i])->MSBcell_LTD.writeVoltageSquareSum;

         subArray->numWritePulse = sumNumWritePulse / subArray->numCol;
         if (static_cast<HybridCell*>(array->cell[0][0])->MSBcell_LTP.nonIdenticalPulse){ 
         // Non-identical write pulse scheme
            if (sumNumWritePulse > 0) 
                subArray->cell.writeVoltage = sqrt(writeVoltageSquareSumRow /sumNumWritePulse);	
            else 
                subArray->cell.writeVoltage = 0;
        }    
         numWriteCellPerOperation = (double)numWriteCellPerOperation/numWriteOperationPerRow;
    }
    subArray->transferWriteLatency += (maxLatencyLTP + maxLatencyLTD);
    subArray->transferWriteDynamicEnergy += NeuroSimSubArrayWriteEnergy(subArray, numWriteOperationPerRow, numWriteCellPerOperation);
 }
 array->transferEnergy= array->transferReadEnergy+array->transferWriteEnergy;
 subArray->transferDynamicEnergy = subArray->transferWriteDynamicEnergy+subArray->transferReadDynamicEnergy;
 subArray->transferLatency = subArray->transferWriteLatency+subArray->transferReadLatency;
}
