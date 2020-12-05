#include <ap_axi_sdata.h>

#define BATCH_SIZE 32
#define M_outputs 2
#define N_hidden 16
#define N_inputs 7
#define numReluLayers 2
#define numHiddenLayers 1

#define size_y BATCH_SIZE * (M_outputs)
#define size_a BATCH_SIZE * (N_hidden * numReluLayers)
#define size_z BATCH_SIZE * (N_hidden * (numHiddenLayers+1))
#define size_x BATCH_SIZE * (N_inputs)
#define size_w (M_outputs * N_hidden) + (N_hidden * N_hidden * numHiddenLayers)

#define SizeDataIn size_y * 2 + size_a + size_z + size_x + size_w // w
#define SizeDataOut (M_outputs) * (N_hidden + 1) + (N_hidden) * (N_hidden + 1) * numHiddenLayers + (N_hidden) * (N_inputs + 1)


struct datatype {
  float data;
  bool last;
};

typedef ap_fixed<32,16> stream_fixed_type;

void backward_lite(datatype in_stream[SizeDataIn],
				   datatype out_stream[SizeDataOut]) {

	#pragma HLS INTERFACE ap_ctrl_none port=return
	#pragma HLS INTERFACE axis port=in_stream
	#pragma HLS INTERFACE axis port=out_stream

    int LIM1 = size_y;      // y
    int LIM2 = size_y * 2;   // y_hat
    int LIM3 = size_y * 2 + size_a;   // a
    int LIM4 = size_y * 2 + size_a + size_z;   // z
    int LIM5 = size_y * 2 + size_a + size_z + size_x; // x
    int LIM6 = size_y * 2 + size_a + size_z + size_x + size_w; // w

    int LIM2W = (M_outputs) * (N_hidden);
    int LIM2b = (M_outputs) * (N_hidden + 1);

    int LIM1W = (M_outputs) * (N_hidden + 1) + (N_hidden) * (N_hidden);
    int LIM1b = (M_outputs) * (N_hidden + 1) + (N_hidden) * (N_hidden + 1);

    int LIM0W = (M_outputs) * (N_hidden + 1) + (N_hidden) * (N_hidden + 1) + (N_hidden) * (N_inputs);
    int LIM0b = (M_outputs) * (N_hidden + 1) + (N_hidden) * (N_hidden + 1) + (N_hidden) * (N_inputs + 1);

	stream_fixed_type dataOut[SizeDataOut] = { };
	bool dataOut_last[SizeDataOut] = { };
	dataOut_last[SizeDataOut - 1] = 1;



	stream_fixed_type smooth_grad[size_y] = { } ;
	stream_fixed_type nn_out_mat[size_y] = { };
	stream_fixed_type batch_y_mat[size_y] = { };
	stream_fixed_type batch_a_mat[size_a] = { };
	stream_fixed_type batch_z_mat[size_z] = { };
	stream_fixed_type batch_x_mat[size_x] = { };
	stream_fixed_type batch_w_mat[size_w] = { };

	stream_fixed_type dif = 0;
	stream_fixed_type abs_dif = 0;

	int i, j, k, k0;

	for (i = 0; i < LIM1; i++) {
//		#pragma HLS PIPELINE
		nn_out_mat[i] = in_stream[i].data; // NN output[M_outputs * BATCH_SIZE]
	}

	for (i = LIM1; i < LIM2; i++) {
//		#pragma HLS PIPELINE
		batch_y_mat[i-LIM1] = in_stream[i].data; // Y output data[M_outputs * BATCH_SIZE]
		dif = (batch_y_mat[i] - nn_out_mat[i]) * (stream_fixed_type)(1.0 / BATCH_SIZE);
		if (dif < 0) {
			abs_dif = -dif;
		} else {
			abs_dif = dif;
		}
		if (abs_dif < 1) {
			smooth_grad[i] = dif;
		} else {
		  // To prevent exploding gradient?
			smooth_grad[i] = ( (dif) / (abs_dif) );
		}
	}

    for (i = LIM2; i < LIM3; i++) {
//        #pragma HLS PIPELINE
        batch_a_mat[i-LIM2] = in_stream[i].data; // a Input data[N_hidden * BATCH_SIZE]
    }

    for (i = LIM3; i < LIM4; i++) {
//        #pragma HLS PIPELINE
        batch_z_mat[i-LIM3] = in_stream[i].data; // Z Input data[N_hidden * BATCH_SIZE]
    }

    for (i = LIM4; i < LIM5; i++) {
//        #pragma HLS PIPELINE
        batch_x_mat[i-LIM4] = in_stream[i].data; // X Input data[N_inputs * BATCH_SIZE]
    }

    for (i = LIM5; i < LIM6; i++) {
//        #pragma HLS PIPELINE
        batch_w_mat[i-LIM5] = in_stream[i].data; // w Input data[N_hidden * M_outputs]
    }


	// Force init to zero
	for(i = 0; i < SizeDataOut; i++){
//		#pragma HLS unroll factor=2
		dataOut[i] = 0;
	}

    int dReLU1 = 0;
    int dReLU2 = 0;

	for (int batchIndex = 0; batchIndex < BATCH_SIZE; batchIndex++) {
//		#pragma HLS unroll factor=2
		for(int i = 0; i < M_outputs; i++){
//			#pragma HLS unroll factor=2
			// Update layer2 b_grad
			dataOut[LIM2W + i] = dataOut[LIM2W + i]
			+ (stream_fixed_type)smooth_grad[batchIndex + i * BATCH_SIZE];

			for(int j = 0; j < N_hidden; j++){
//#pragma HLS unroll factor=2
				// Update layer1 b_grad
				// ReLU based on z[1]
				if(batch_z_mat[batchIndex + j * BATCH_SIZE + BATCH_SIZE * N_hidden] > 0)dReLU1 = 1;
				else dReLU1 = 0;
				dataOut[LIM1W + j] = dataOut[LIM1W + j]
				+ (stream_fixed_type)smooth_grad[batchIndex + i * BATCH_SIZE]
				* batch_w_mat[i + j * M_outputs + N_hidden * N_hidden] // w[1]
				* dReLU1;

				// Update layer2 W_grad
				dataOut[j + i * N_hidden] = dataOut[j + i * N_hidden]
				+ (stream_fixed_type)(smooth_grad[batchIndex + i * BATCH_SIZE]
				* batch_a_mat[batchIndex + j * BATCH_SIZE + N_hidden * BATCH_SIZE]); //a[1]

				for(int k = 0; k < N_hidden; k++){
//#pragma HLS unroll factor=2
//#pragma HLS PIPELINE
					// Update Layer0 b_grad
					// ReLU based on z[0]
					if(batch_z_mat[batchIndex + k * BATCH_SIZE] > 0)dReLU2 = 1;
					else dReLU2 = 0;
					dataOut[LIM0W + k] = dataOut[LIM0W + k]
					+ (stream_fixed_type)smooth_grad[batchIndex + i * BATCH_SIZE]
					* batch_w_mat[i + j * M_outputs + N_hidden * N_hidden] // w[1]
					* batch_w_mat[j + k * N_hidden] // w[0]
					* dReLU1
					* dReLU2;

					// Update layer1 W_grad
					dataOut[LIM2b + k + j * N_hidden] = dataOut[LIM2b + k + j * N_hidden]
					+ smooth_grad[batchIndex + i * BATCH_SIZE]
					* batch_w_mat[i + j * M_outputs + N_hidden * N_hidden]
					* batch_z_mat[batchIndex + k * BATCH_SIZE]
					* dReLU1;

					for(int k0 = 0; k0 < N_inputs; k0++){
//#pragma HLS unroll factor=2
						// Update layer0 W_grad
						dataOut[LIM1b + k0 + k * N_inputs] = dataOut[LIM1b + k0 + k * N_inputs]
						+ (stream_fixed_type)smooth_grad[batchIndex + i * BATCH_SIZE]
						* batch_w_mat[i + j * M_outputs + N_hidden * N_hidden] // w[1]
						* batch_w_mat[j + k * N_hidden] // w[0]
						* dReLU1
						* dReLU2
						* batch_x_mat[batchIndex + k0 * BATCH_SIZE];
					}
				}
			}
		}
	}

	for(int i = 0; i < SizeDataOut; i++){
//#pragma HLS PIPELINE
		out_stream[i].data = dataOut[i];
		out_stream[i].last = dataOut_last[i];
	}

}
