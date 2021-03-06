/**
 * Copyright (C) 2012-2014 Analog Devices, Inc.
 *
 * Licensed under the GPL-2.
 *
 */

#include <matio.h>

static short convert(double scale, float val)
{
	return (short) (val * scale);
}

static int analyse_wavefile(const char *file_name, char **buf, int *count, int tx)
{
	int ret, j, i = 0, size, rep;
	double max = 0.0, val[4], scale = 0.0;
	double i1, q1, i2, q2;
	char line[80];
	mat_t *matfp;
	matvar_t **matvars;

	FILE *infile = fopen(file_name, "r");

	*buf = NULL;

	if (infile == NULL)
		return -3;

	if (fgets(line, 80, infile) != NULL) {
		if (strncmp(line, "TEXT", 4) == 0) {
			/* Unscaled samples need to be in the range +- 2047 */
			if (strncmp(line, "TEXTU", 5) == 0)
				scale = 16.0;	/* scale up to 16-bit */
			ret = sscanf(line, "TEXT%*c REPEAT %d", &rep);
			if (ret != 1) {
				rep = 1;
			}
			size = 0;
			while (fgets(line, 80, infile)) {
				ret = sscanf(line, "%lf%*[, \t]%lf%*[, \t]%lf%*[, \t]%lf",
						&val[0], &val[1], &val[2], &val[3]);

				if (!(ret == 4 || ret == 2)) {
					fclose(infile);
					return -2;
				}

				for (i = 0; i < ret; i++)
					if (fabs(val[i]) > max)
						max = fabs(val[i]);

				size += ((tx == 2) ? 8 : 4);


			}

			size *= rep;
			if (scale == 0.0)
				scale = 32767.0 / max;

			if (max > 32767.0)
				fprintf(stderr, "ERROR: DAC Waveform Samples > +/- 2047.0\n");

			*buf = malloc(size);
			if (*buf == NULL)
				return 0;

			unsigned long long *sample = *((unsigned long long **) buf);
			unsigned int *sample_32 = *((unsigned int **) buf);

			rewind(infile);

			if (fgets(line, 80, infile) != NULL) {
				if (strncmp(line, "TEXT", 4) == 0) {
					size = 0;
					i = 0;
					while (fgets(line, 80, infile)) {

						ret = sscanf(line, "%lf%*[, \t]%lf%*[, \t]%lf%*[, \t]%lf",
								&i1, &q1, &i2, &q2);
						for (j = 0; j < rep; j++) {
							if (ret == 4 && tx == 2) {
								sample[i++] = ((unsigned long long) convert(scale, q2) << 48) +
								    ((unsigned long long) convert(scale, i2) << 32) +
								    (convert(scale, q1) << 16) +
								    (convert(scale, i1) << 0);

								size += 8;
							}
							if (ret == 2 && tx == 2) {
								sample[i++] = ((unsigned long long) convert(scale, q1) << 48) +
								    ((unsigned long long) convert(scale, i1) << 32) +
								    (convert(scale, q1) << 16) +
								    (convert(scale, i1) << 0);

								size += 8;
							}
							if (tx == 1) {
								sample_32[i++] = (convert(scale, q1) << 16) +
									(convert(scale, i1) << 0);

								size += 4;
							}
						}
					}
				}
			}

			fclose(infile);
			*count = size;

		} else {
			fclose(infile);
			ret = 0;
			/* Is it a MATLAB file?
			 * http://na-wiki.csc.kth.se/mediawiki/index.php/MatIO
			 */
			matfp = Mat_Open(file_name, MAT_ACC_RDONLY);
			if (matfp == NULL)
				return -3;

			rep = 0;
			matvars = malloc(sizeof(matvar_t *) * tx);

			while (rep < tx && (matvars[rep] = Mat_VarReadNextInfo(matfp)) != NULL) {
				/* must be a vector */
				if (matvars[rep]->rank !=2 || (matvars[rep]->dims[0] > 1 && matvars[rep]->dims[1] > 1))
					return -1;
				/* should be a double */
				if (matvars[rep]->class_type != MAT_C_DOUBLE)
					return -1;
/*
	printf("%s : %s\n", __func__, matvars[rep]->name);
	printf("  rank %d\n", matvars[rep]->rank);
	printf("  dims %d x %d\n", matvars[rep]->dims[0], matvars[rep]->dims[1]);
	printf("  data %d\n", matvars[rep]->data_type);
	printf("  class %d\n", matvars[rep]->class_type);
*/
				Mat_VarReadDataAll(matfp, matvars[rep]);

				if (matvars[rep]->isComplex) {
					struct ComplexSplit *complex_data = matvars[rep]->data;
					double *re, *im;
					re = complex_data->Re;
					im = complex_data->Im;

					for (j = 0; j < matvars[rep]->dims[0] ; j++) {
						 if (fabs(re[j]) > max)
							 max = fabs(re[j]);
						 if (fabs(im[j]) > max)
							 max = fabs(im[j]);
					}

				} else {
					double re;

					for (j = 0; j < matvars[rep]->dims[0] ; j++) {
						re = ((double *)matvars[rep]->data)[j];
						if (fabs(re) > max)
							max = fabs(re);
					}
				}
				rep++;
			}
			rep--;

//	printf("read %i vars, length %i, max value %f\n", rep, matvars[rep]->dims[0], max);

			if (max <= 1.0)
				max = 1.0;
			scale = 32767.0 / max;

			if (max > 32767.0) {
				fprintf(stderr, "ERROR: DAC Waveform Samples > +/- 2047.0\n");
			}

			size = matvars[0]->dims[0];

			for (i = 0; i <= rep; i++) {
				if (size != matvars[i]->dims[0]) {
					printf("dims don't match\n");
					return -1;
				}
			}

			*buf = malloc((size + 1) * ((tx == 2) ? 8 : 4));

			if (*buf == NULL) {
				printf("error %s:%d\n", __func__, __LINE__);
				return 0;
			}

			*count = size * ((tx == 2) ? 8 : 4);

			unsigned long long *sample = *((unsigned long long **) buf);
			unsigned int *sample_32 = *((unsigned int **) buf);
			double *re1, *im1, *re2, *im2;
			struct ComplexSplit *complex_data1, *complex_data2;

			complex_data1 = matvars[0]->data;

			if (rep == 0) {
				complex_data2 = matvars[0]->data;
			} else if (rep == 1) {
				complex_data2 = matvars[1]->data;
			}

			if (rep == 0 && matvars[0]->isComplex) {
				/* One complex vector */
				re1 = complex_data1->Re;
				im1 = complex_data1->Im;
				re2 = complex_data1->Re;
				im2 = complex_data1->Im;
			} else if (rep == 1 && matvars[0]->isComplex && matvars[1]->isComplex) {
				/* Dual complex */
				re1 = complex_data1->Re;
				im1 = complex_data1->Im;
				re2 = complex_data2->Re;
				im2 = complex_data2->Im;
			} else if (rep == 1 && !matvars[0]->isComplex && !matvars[1]->isComplex) {
				/* One real, one imaginary */
				re1 = ((double *)matvars[0]->data);
				im1 = ((double *)matvars[1]->data);
				re2 = ((double *)matvars[0]->data);
				im2 = ((double *)matvars[1]->data);
			} else if (rep == 3 && !matvars[0]->isComplex && !matvars[1]->isComplex &&
					!matvars[2]->isComplex && !matvars[3]->isComplex) {
				/* Dual real / imaginary */
				re1 = ((double *)matvars[0]->data);
				im1 = ((double *)matvars[1]->data);
				re2 = ((double *)matvars[2]->data);
				im2 = ((double *)matvars[3]->data);
			} else {
				printf("Don't understand file type\n");
				free(*buf);
				*buf = NULL;
				ret = -1;
				goto matvar_free;
			}

			if (tx == 2) {
				 for (i = 0 ; i < size; i++) {
					sample[i] = ((unsigned long long) convert(scale, im2[i]) << 48) +
						    ((unsigned long long) convert(scale, re2[i]) << 32) +
									 (convert(scale, im1[i]) << 16) +
									 (convert(scale, re1[i]) << 0);
				 }
			} else if (tx == 1) {
				for (i = 0 ; i < size; i++) {
					sample_32[i] = (convert(scale, im1[i]) << 16) +
						       (convert(scale, re1[i]) << 0);
				}
			}
		matvar_free:
			for (j = 0; j <= rep; j++) {
				Mat_VarFree(matvars[j]);
			}
			free(matvars);
			Mat_Close(matfp);
			return ret;
		}
	} else {
		fclose(infile);
		return -1;
	}
	return 0;
}
