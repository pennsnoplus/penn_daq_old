int fit_caldac()
{
    gROOT->Reset();
    ifstream fin("./cald_test.log");

    int slot_mask;
    int adccounts[4096];
    int adcs[4][4096];
    int numpoints;
    int i,j;
    int adccount,s,adc[4];
    int slot;

    fin >> slot_mask >> numpoints;
    numpoints--;
    TGraph *g[16][4];
    TF1 *f[16][4];
    TMultiGraph *mg[16];
    TCanvas *c[16];
    double chi2[16][4],par0[16][4],par1[16][4];
    int endpoint[4];
    char fname[10];
    char cname[10];

    for (slot = 0;slot<16;slot++){
        if ((0x1<<slot) & slot_mask){
            for (i=0;i<4;i++){
                endpoint[i] = 4096;
            }
            printf("slot = %d\n",slot);
            for (i=-1;i<numpoints;i++){
                fin >> adccount >> s >> adc[0] >> adc[1] >> adc[2] >> adc[3];
                if (i>=0){
                    adccounts[i] = adccount;
                    for (j=0;j<4;j++){
                        adcs[j][i] = adc[j];
                        if (adc[j] == 0 && adccount < endpoint[j])
                            endpoint[j] = adccount;
                    }
                }
            }
            mg[slot] = new TMultiGraph();
            sprintf(cname,"slot_%d",slot);
            c[slot] = new TCanvas(cname,cname,200,10,700,500);
            for (j=0;j<4;j++){
                g[slot][j] = new TGraph(numpoints,adccounts,adcs[j]);
                g[slot][j]->SetMarkerStyle(6);
                g[slot][j]->SetMarkerColor(j+2);
                sprintf(fname,"f_%d_%d",slot,j);
                f[slot][j] = new TF1(fname,"pol1",adccounts[10],endpoint[j]);
                g[slot][j]->Fit(fname,"SR");
                chi2[slot][j] = f[slot][j]->GetChisquare();
                par0[slot][j] = f[slot][j]->GetParameter(0);
                par1[slot][j] = f[slot][j]->GetParameter(1);
                mg[slot]->Add(g[slot][j]);
            }
            mg[slot]->Draw("AP");
        }
    }
    for (slot=0;slot<16;slot++){
        if ((0x1<<slot) & slot_mask){
            for (i=0;i<4;i++){
                printf("For slot %d adc %d, fit %6.2f + %6.2f x with chi2 of %6.2f\n",slot,i,par0[slot][i],par1[slot][i],chi2[slot][i]);
            }
        }
    }
}
