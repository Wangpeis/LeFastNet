#include "stdafx.h"
#include "ConvolutionWeights.h"

using namespace NeuralNetworkNative;

ConvolutionWeights::ConvolutionWeights(int convolutionWidth, int convolutionHeight, int mapCount) 
	: RectangularWeights(convolutionWidth, convolutionHeight, mapCount), 
	Weight(std::vector<double>(convolutionWidth * convolutionHeight * mapCount)),
	WeightStepSize(std::vector<double>(convolutionWidth * convolutionHeight * mapCount))
{
	Weights::Randomise(Weight, Weight.size());
	Weights::Clear(WeightStepSize);
}

void ConvolutionWeights::PropogateForward(RectangularStep& downstream, int mapNumber)
{
	assert(((size_t)mapNumber) < downstream.Upstream.size() && mapNumber >= 0);
	RectangularStep& upstream = *downstream.Upstream[mapNumber];

	int index = 0;
	for (int y = 0; y < downstream.Height; y++)
	{
		for (int x = 0; x < downstream.Width; x++)
		{
			downstream.WeightedInputs[index++] += PropogateForward(upstream, x, y, mapNumber);
		}
	}
}

double ConvolutionWeights::PropogateForward(RectangularStep& upstream, int downstreamX, int downstreamY, int mapNumber)
{
	assert(downstreamX + Width <= upstream.Width);   // Check we are staying within the width limit of the step.
	assert(downstreamY + Height <= upstream.Height);  // Check we are staying within the height limit of the step.

	double result = Bias;

	int upstreamIndex = (downstreamY * upstream.Width) + downstreamX;
	int weightIndex = mapNumber * Width * Height;

	for (int y = 0; y < Height; y++)
	{
		for (int x = 0; x < Width; x++)
		{
			result += upstream.Output[upstreamIndex] * Weight[weightIndex];
			upstreamIndex += 1;
			weightIndex += 1;
		}
		upstreamIndex += upstream.Width - Width;
	}
	return result;
}

void ConvolutionWeights::StartPreTrainingCore()
{
	RectangularWeights::StartPreTrainingCore();
	Weights::Clear(WeightStepSize);
}

void ConvolutionWeights::PropogateUnitSecondDerivatives(RectangularStep &downstream, int mapNumber)
{
	RectangularStep& upstream = *downstream.Upstream[mapNumber];

	for (int y = 0; y < Height; y++)
	{
		for (int x = 0; x < Width; x++)
		{
			PropogateUnitSecondDerivatives(upstream, downstream, x, y, mapNumber);
		}
	}
}

void ConvolutionWeights::PropogateUnitSecondDerivatives(RectangularStep &upstream, RectangularStep &downstream, int weightX, int weightY, int mapNumber)
{
	double weight2ndDerivative = 0;

	int weightIndex = mapNumber * Width * Height + weightY * Width + weightX;
	double weight = Weight[weightIndex];

	int downstreamIndex = 0;
	int upstreamIndex = (weightY * upstream.Width) + weightX;

	// This loop here is equivalent to the sigma in equation 19 in Gradient-Based Learning Applied to Document Recognition.
	for (int y = 0; y < downstream.Height; y++)
	{
		for (int x = 0; x < downstream.Width; x++)
		{
			double upstreamInput = upstream.Output[upstreamIndex];
			double downstreamError2ndDerivative = downstream.ErrorDerivative[downstreamIndex]; // (d^2)E/(dAj)^2, where Aj is the sum of inputs to this downstream unit.

			// Here we calculate (d^2)Ej/(dWij)^2 by multiplying the 2nd derivative of E with respect to the sum of inputs, Aj   
			// by the state of Oi, the upstream unit, squared. Refer to Equation 25 in document.
			// The summing happening here is described by equation 23.
			weight2ndDerivative += downstreamError2ndDerivative * upstreamInput * upstreamInput;

			// This is implementing the last sigma of Equation 27.
			// This propogates error second derivatives back to previous layer, but will need to be multiplied by the second derivative
			// of the activation function at the previous layer.
			upstream.ErrorDerivative[upstreamIndex] += weight * weight * downstreamError2ndDerivative;

			downstreamIndex += 1;
			upstreamIndex += 1;
		}
		upstreamIndex += Width - 1; // Equal to: upstream.Width - downstream.Width;
	}

	WeightStepSize[weightIndex] += weight2ndDerivative;
}

void ConvolutionWeights::EstimateBiasSecondDerivative(RectangularStep &downstream)
{
	for (int i = 0; i < downstream.Length; i++)
	{
		// Calculating the sum of: second derivatives of error with respect to the bias weight.
		// Note that the bias is implemented as an always-on Neuron with a (the same) weight to the outputs neurons.
		BiasStepSize += downstream.ErrorDerivative[i] * 1.0 * 1.0;
	}
}

void ConvolutionWeights::CompletePreTrainingCore()
{
	double sampleCount = (double)preTrainingSamples;

	// Divide each of the 2nd derivative sums by the number of samples used to make the estimation, then  convert into step size.

	BiasStepSize = learningRate / (mu + (BiasStepSize / sampleCount));

	double averageHkk = 0.0;
	for (int i = 0; i < Size; i++)
	{
		double hkk = WeightStepSize[i] / sampleCount;
		averageHkk += hkk;
		WeightStepSize[i] = learningRate / (mu + hkk);
	}
}


void ConvolutionWeights::PropogateError(RectangularStep &downstream, int mapNumber)
{
	RectangularStep &upstream = *downstream.Upstream[mapNumber];

	for (int y = 0; y < Height; y++)
	{
		for (int x = 0; x < Width; x++)
		{
			PropogateErrors(upstream, downstream, x, y, mapNumber);
		}
	}
}

void ConvolutionWeights::PropogateErrors(RectangularStep &upstream, RectangularStep &downstream, int weightX, int weightY, int mapNumber)
{
	int weightIndex = mapNumber * Width * Height + weightY * Width + weightX;
	double weight = Weight[weightIndex];

	int downstreamIndex = 0;
	double weightError = 0.0;
	int upstreamIndex = (weightY * upstream.Width) + weightX;
	double weightStepSize = WeightStepSize[weightIndex];

	// This loop here is equivalent to the sigma in equation 19 in Gradient-Based Learning Applied to Document Recognition.
	for (int y = 0; y < downstream.Height; y++)
	{
		for (int x = 0; x < downstream.Width; x++)
		{
			double upstreamState = upstream.Output[upstreamIndex];
			double downstreamErrorDerivative = downstream.ErrorDerivative[downstreamIndex];

			// Calculate inputs error gradient by taking the sum, for all outputs of
			// dEk/dAj multiplied by dAj/dOj (w/sum =dEj/dOj);
			double inputError = downstreamErrorDerivative * weight;
			upstream.ErrorDerivative[upstreamIndex] += inputError;

			// Calculate the Weight's first derivative with respect to the error
			double weightErrorGradient = downstreamErrorDerivative * upstreamState;
			weightError += weightErrorGradient;

			downstreamIndex += 1;
			upstreamIndex += 1;
		}
		upstreamIndex += Width - 1; // Equal to: upstream.Width - downstream.Width;
	}

	double deltaWeight = weightError * weightStepSize;
	Weight[weightIndex] -= deltaWeight;
}

ConvolutionWeights::~ConvolutionWeights()
{
}
