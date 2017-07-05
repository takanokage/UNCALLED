#include <iostream>
#include <fstream>
#include <algorithm>
#include <tuple>
#include <vector>
#include <math.h>
#include <stddef.h>
#include <list>
#include <unordered_map>
#include "timer.h"
#include "nano_bwt.hpp"
#include "boost/math/distributions/students_t.hpp"

#define MER_LEN 6
#define PI 3.1415926535897


const int alph_size = (int) pow(4, MER_LEN);
int BASE_IDS[(int) ('t'+1)];
std::vector< std::vector<mer_id> > mer_neighbors;
mer_id mer_to_id(std::string &seq, unsigned int i);
void init_base_ids();


//Reads a model directly from a file and creates the FM index from the given reference
NanoFMI::NanoFMI(std::ifstream &model_in, std::vector<mer_id> &mer_seq, int tally_sp) {

    double em_mean, em_stdev, es_mean, es_stdev;
    
    std::cerr << "Reading model\n";

    for (int i = 0; i < alph_size; i++) {
        model_in >> em_mean >> em_stdev >> es_mean >> es_stdev;
        em_means.push_back(em_mean);
        em_stdevs.push_back(em_stdev);
        es_means.push_back(es_mean);
        es_stdevs.push_back(es_stdev);
    }

    //Makes 4096 ID have lowest lexicographic order
    em_means.push_back(-1);
    em_stdevs.push_back(-1);
    es_means.push_back(-1);
    es_stdevs.push_back(-1);

    tally_dist = tally_sp;
    
    mer_seq_tmp = &mer_seq;

    // create bwt
    make_bwt();
}

//Copies a model from a vector and creates the FM index from the given refernce
NanoFMI::NanoFMI(std::vector<double> model_in, std::vector<mer_id> &mer_seq, int tally_sp) {
    
    std::cerr << "Reading model\n";

    for (int i = 0; i < alph_size; i++) {
        em_means.push_back(model_in[4*i+0]);
        em_stdevs.push_back(model_in[4*i+1]);
        es_means.push_back(model_in[4*i+2]);
        es_stdevs.push_back(model_in[4*i+3]);
    }

    //Makes 4096 ID have lowest lexicographic order
    em_means.push_back(-1);
    em_stdevs.push_back(-1);
    es_means.push_back(-1);
    es_stdevs.push_back(-1);

    tally_dist = tally_sp;
    
    mer_seq_tmp = &mer_seq;

    // create bwt
    make_bwt();
}

//Creates the BWT and other FM index structures from the temporary mer sequence
void NanoFMI::make_bwt() {
    
    //For outputting time
    Timer timer;

    //Init suffix array
    //Not using suffix_ar instance var speeds up sorting significantly
    std::vector<unsigned int> suffix_ar_tmp(mer_seq_tmp->size());
    for (unsigned int i = 0; i < suffix_ar_tmp.size(); i++) 
        suffix_ar_tmp[i] = i;

    std::cerr << "SA init time: " << timer.lap() << "\n";

    //Create the suffix array
    std::sort(suffix_ar_tmp.begin(), suffix_ar_tmp.end(), *this);
    suffix_ar.swap(suffix_ar_tmp);

    std::cerr << "SA sort time: " << timer.lap() << "\n";

    //Allocate space for other datastructures
    bwt.resize(mer_seq_tmp->size());
    mer_f_starts.resize(alph_size);
    mer_counts.resize(alph_size);
    mer_tally.resize(alph_size);

    for (mer_id i = 0; i < alph_size; i++)
        mer_tally[i].resize((mer_seq_tmp->size() / tally_dist) + 1, -1);
    
    std::cerr << "FM init time: " << timer.lap() << "\n";

    int tally_mod = tally_dist;
    
    //Single pass to generate BWT and other datastructures
    for (unsigned int i = 0; i < suffix_ar.size(); i++) {
        
        //Fill in BWT
        if (suffix_ar[i] > 0)
            bwt[i] = mer_seq_tmp->at(suffix_ar[i]-1);
        else
            bwt[i] = mer_seq_tmp->at(suffix_ar[suffix_ar.size()-1]);

        //Update 6-mer counts
        mer_counts[bwt[i]]++;
        
        //Update tally array
        if (tally_mod == tally_dist) {
            for (mer_id j = 0; j < alph_size; j++)
                mer_tally[j][i / tally_dist] = mer_counts[j];
            tally_mod = 0;
        }
        tally_mod += 1;
    }

    std::cerr << "FM build time: " << timer.lap() << "\n";
    
    //Compute start locations for F array
    for (mer_id i = 0; i < alph_size; i++) {
        mer_f_starts[i] = 1;
        for (mer_id j = 0; j < alph_size; j++)
            if (signal_compare(i, j) > 0)
                mer_f_starts[i] += mer_counts[j];
    }
    
    //Fill in last entry in tally array if needed
    if (mer_seq_tmp->size() % tally_dist == 0)
        for (mer_id i = 0; i < alph_size; i++)
            mer_tally[i][mer_tally[i].size()-1] = mer_counts[i];

}

//Returns true if the suffix of *mer_seq_tmp starting at rot1 is less than that
//starting at rot2. Used to build suffix array.
bool NanoFMI::operator() (unsigned int rot1, unsigned int rot2) {
    
    int c;
    for (unsigned int i = 0; i < mer_seq_tmp->size(); i++) {
        c = signal_compare(mer_seq_tmp->at(rot1+i), mer_seq_tmp->at(rot2+i));
        
        if (c == 0)
            continue;

        if (c < 0)
            return true;

       return false;
    }

    return false;
}

//Compres the signals corrasponding to the given k-mer IDs
//Returns -1 if mer1 < mer2, 1 if mer1 > mer2, 0 if mer1 == mer2
//Used to build suffix array
int NanoFMI::signal_compare(mer_id mer1, mer_id mer2) {
    if (em_means[mer1] < em_means[mer2])
        return -1;
    else if (em_means[mer1] > em_means[mer2])
        return 1;

    if (es_means[mer1] < es_means[mer2])
        return -1;
    else if (es_means[mer1] > es_means[mer2])
        return 1;

    if (em_stdevs[mer1] < em_stdevs[mer2])
        return 1;
    else if (em_stdevs[mer1] > em_stdevs[mer2])
        return -1;

    if (es_stdevs[mer1] < es_stdevs[mer2])
        return 1;
    else if (es_stdevs[mer1] > es_stdevs[mer2])
        return -1;

    return 0;
}

/*
double NanoFMI::t_test(Event e, int i, ScaleParams scale) {
    // scale model mean
    double model_mean = em_means[i] * scale.scale + scale.shift;
    double model_stdv = em_stdevs[i] * scale.var;

    // calculate t-stat
    // https://en.wikipedia.org/wiki/Student%27s_t-test#Equal_or_unequal_sample_sizes.2C_equal_variance

    double s_p = sqrt((pow(model_stdv, 2) + pow(e.stdv, 2))/2.0);
    double t = (e.mean - model_mean) / (sqrt(2.0/e.length) * s_p);
    int df = (e.length * 2) - 2;
    boost::math::students_t dist(df);
    // probability that the difference is due to chance
    double q = boost::math::cdf(boost::math::complement(dist, fabs(t)));
    // std::cerr << "q: " << q << std::endl;
    return q;
}*/

float inv_gauss(float x, float mean, float lambda) {
    return sqrt(lambda / (2 * PI * pow(x, 3)))  * exp(-lambda * pow(x - mean, 2) / (2 * x * pow(mean, 2)));
}

float gauss(float x, float mean, float stdv) {
    return exp( -pow(x - mean, 2) / (2 * pow(stdv, 2)) ) / sqrt(2 * PI * pow(stdv, 2));
}

bool NanoFMI::t_test(Event e, int i, ScaleParams scale) {
    float scaled_mean = em_means[i] * scale.scale + scale.shift,
           lambda = pow((float)es_means[i], 3) / pow((float)es_stdevs[i], 2);

    return gauss(e.mean, scaled_mean, em_stdevs[i])*inv_gauss(e.stdv, es_means[i], lambda) >= 0.0001;
}

//Returns a vector of MerRanges with empty ranges, each containing a unique
//k-mer that matches the given event
std::vector<MerRanges> NanoFMI::match_event(Event e, ScaleParams scale) {
    std::vector<MerRanges> mers;
    for (mer_id i = 0; i < em_means.size(); i++) {
        double alpha = 0.05;
        if (t_test(e, i, scale)) {
            MerRanges m = {i, std::vector<int>()};
            mers.push_back(m);
        }
    }
    return mers;
}

//Returns the distance from a BWT index to the nearest tally array checkpoint
int NanoFMI::tally_cp_dist(int i) {
    int cp = (i / tally_dist)*tally_dist; //Closest checkpoint < i

    //Check if checkpoint after i is closer
    if (i - cp > (cp + tally_dist) - i && cp + (unsigned) tally_dist < bwt.size())
        cp += tally_dist;

    return cp - i;
}

//Returns the number of occurences of the given k-mer in the BWT up to and
//including the given index
int NanoFMI::get_tally(mer_id c, int i) {
    if (i < 0)
        return -1;
    int cp_dist = tally_cp_dist(i);
    int tally = mer_tally[c][(i + cp_dist) / tally_dist];

    if (cp_dist > 0) {
        for (int j = i+1; j <= i + cp_dist; j++)
            if (bwt[j] == c)
                tally--;

    } else if (cp_dist < 0) {
        for (int j = i; j > i + cp_dist; j--)
            if (bwt[j] == c)
                tally++;
    }

    return tally;
}

//Aligns the vector of events to the reference using LF mapping
//Returns the number of exact alignments
int NanoFMI::lf_map(std::vector<Event> events, ScaleParams scale) {

    //Stores ranges corrasponding to the F array and the L array (the BWT)
    std::unordered_map< mer_id, std::vector<int> > f_locs, l_locs;

    //Match the first event

    //f_locs = match_event(events.back(), scale);

    std::vector<int> range(2);
    for (mer_id i = 0; i < em_means.size(); i++) {
        if (t_test(events.back(), i, scale)) {
            range[0] = mer_f_starts[i];
            range[1] = mer_f_starts[i] + mer_counts[i]-1;
            f_locs[i] = range;
        }
    }

    //Find the intial range for each matched k-mer
    //for (unsigned int f = 0; f < f_locs.size(); f++) {
    //    f_locs[f].ranges.push_back(mer_f_starts[f_locs[f].mer]);
    //    f_locs[f].ranges.push_back(mer_f_starts[f_locs[f].mer] 
    //                               + mer_counts[f_locs[f].mer]-1);
    //}

    bool mer_matched = false;
    //std::cout << "matching\n";
    
    //Match all other events backwards
    for (int i = events.size()-2; i >= 0; i--) {
        mer_matched = false;

        //Find all candidate k-mers
        for (auto it = f_locs.begin(); it != f_locs.end(); ++it) {
            std::vector<mer_id> &neighbors = mer_neighbors[it->first];

            for (int n = 0; n < neighbors.size(); n++) {
                bool mer_found = l_locs.find(neighbors[n]) != l_locs.end();

                if(mer_found || t_test(events[i], neighbors[n], scale)) {

                    if (!mer_found) {
                        l_locs[neighbors[n]] = std::vector<int>();
                    }

                    std::vector<int> &ranges = l_locs[neighbors[n]];

                    for (unsigned int r = 0; r < (it->second).size(); r += 2) {
                        int min_tally = get_tally(neighbors[n], (it->second)[r] - 1),
                            max_tally = get_tally(neighbors[n], (it->second)[r + 1]);
                        
                        //Candidate k-mer occurs in the range
                        if (min_tally < max_tally) {
                            ranges.push_back(mer_f_starts[neighbors[n]]+min_tally);
                            ranges.push_back(mer_f_starts[neighbors[n]]+max_tally-1);
                            mer_matched = true;
                            //std::cout << "MATCH " << i << "\n";
                        }
                    }
                }
            }
        }

        //
        ////Check each candidate k-mer
        //for (unsigned int l = 0; l < l_locs.size(); l++) { 

        //    //Check each range from the previous iteration
        //    for(unsigned int f = 0; f < f_locs.size(); f ++) {
        //        for (unsigned int r = 0; r < f_locs[f].ranges.size(); r += 2) {

        //            //Find the min and max tally for the curent k-mer range
        //            //Corrasponds to the min and max rank of the candidate k-mer
        //            int min_tally = get_tally(l_locs[l].mer, f_locs[f].ranges[r] - 1),
        //                max_tally = get_tally(l_locs[l].mer, f_locs[f].ranges[r + 1]);
        //            
        //            //Candidate k-mer occurs in the range
        //            if (min_tally < max_tally) {
        //                l_locs[l].ranges.push_back(mer_f_starts[l_locs[l].mer]+min_tally);
        //                l_locs[l].ranges.push_back(mer_f_starts[l_locs[l].mer]+max_tally-1);
        //                mer_matched = true;
        //            }
        //        }
        //    }
        //}

        //Update current ranges
        f_locs.swap(l_locs);

        l_locs.clear();
        
        //Stop search if not matches found
        if (!mer_matched)
            break;

    }
    
    //Count up the number of alignments
    //Currently not doing anything with location of alignments
    int count = 0;
    if (mer_matched) {
        for (auto it = f_locs.begin(); it != f_locs.end(); ++it) {
            for (unsigned int r = 0; r < it->second.size(); r += 2) {
                for (int s = it->second[r]; s <= it->second[r+1]; s++) {
                    std::cout << "Match: " << suffix_ar[s] << "\n";
                    count += 1;
                }
            }
        }
    }

    return count;
}


//Converts the 6-mer at the given location to a k-mer ID
mer_id mer_to_id(std::string &seq, unsigned int i) {
    mer_id id = BASE_IDS[(unsigned int) seq[i]];
    for (unsigned int j = 1; j < MER_LEN; j++)
        id = (id << 2) | BASE_IDS[(unsigned int) seq[i+j]];
    return id;
}


//Inits BASE_IDS, which is used to generate k-mer IDs
void init_base_ids() {
    for (unsigned int i = 0; i < ('t'+1); i++)
        BASE_IDS[i] = -1;
    BASE_IDS[(unsigned int) 'A'] = BASE_IDS[(unsigned int) 'a'] = 0;
    BASE_IDS[(unsigned int) 'C'] = BASE_IDS[(unsigned int) 'c'] = 1;
    BASE_IDS[(unsigned int) 'G'] = BASE_IDS[(unsigned int) 'g'] = 2;
    BASE_IDS[(unsigned int) 'T'] = BASE_IDS[(unsigned int) 't'] = 3;

    //TODO: Probably do this when you load the model, idiot

    mer_neighbors.resize(4096);
    std::list<std::string> to_check;
    to_check.push_front("AAAAAA");
    std::string cur_mer;
    int cur_id;

    while (!to_check.empty()) {
        cur_mer = to_check.back();
        to_check.pop_back();
        cur_id = mer_to_id(cur_mer, (unsigned int) 0);

        if (!mer_neighbors[cur_id].empty())
            continue;

        std::string s = "A" + cur_mer.substr(0, 5);
        mer_neighbors[cur_id].push_back(mer_to_id(s, 0));
        to_check.push_front(s);
        s = "C" + cur_mer.substr(0, 5);
        mer_neighbors[cur_id].push_back(mer_to_id(s, 0));
        to_check.push_front(s);
        s = "G" + cur_mer.substr(0, 5);
        mer_neighbors[cur_id].push_back(mer_to_id(s, 0));
        to_check.push_front(s);
        s = "T" + cur_mer.substr(0, 5);
        mer_neighbors[cur_id].push_back(mer_to_id(s, 0));
        to_check.push_front(s);
    }
}

//Returns the complement to the given base
char rev_base(char c) {
    switch (c) {
        case 'A':
        case 'a':
            return 'T';
        case 'C':
        case 'c':
            return 'G';
        case 'G':
        case 'g':
            return 'c';
        case 'T':
        case 't':
            return 'A';
        default:
            return 'N';
    }

    return 'N';
}

//Reads the given fasta file and stores forward and reverse k-mers
void parse_fasta(std::ifstream &fasta_in, 
                 std::vector<mer_id> &fwd_ids, 
                 std::vector<mer_id> &rev_ids) {
    init_base_ids();

    //Used for efficiently generating reverse sequence
    std::list<mer_id> rev_list;

    //For parsing the file
    std::string cur_line, prev_line, fwd_seq, rev_seq;
    
    getline(fasta_in, cur_line); //read past header

    while (getline(fasta_in, cur_line)) {

        //Saves end of previous line to get last five 6-mers
        if (prev_line.size() > 0)
            fwd_seq = prev_line.substr(prev_line.size()-MER_LEN+1) + cur_line;
        else
            fwd_seq = cur_line;
        
        //Store forward IDs
        for (unsigned int i = 0; i < fwd_seq.size() - MER_LEN + 1; i++)
            fwd_ids.push_back(mer_to_id(fwd_seq, i));
    
        //Make reverse complement sequence
        rev_seq = std::string(fwd_seq.size(), ' ');
        for (unsigned int i = 0; i < fwd_seq.size(); i++)
            rev_seq[rev_seq.size()-i-1] = rev_base(fwd_seq[i]);

        //Store reverse IDs
        for (unsigned int i = rev_seq.size() - MER_LEN; i < rev_seq.size(); i--)
            rev_list.push_front(mer_to_id(rev_seq, i));

        prev_line = cur_line;
    }

    //Store reverse IDs in vector
    rev_ids = std::vector<mer_id>(rev_list.begin(), rev_list.end());

    //Add EOF character
    fwd_ids.push_back((int) pow(4, MER_LEN));
    rev_ids.push_back((int) pow(4, MER_LEN));

}

