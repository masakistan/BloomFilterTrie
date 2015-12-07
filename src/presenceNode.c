#include "./../lib/presenceNode.h"

extern void init_resultPresenceSeed(resultPresenceSeed* res_seed);
extern void init_resultPresenceSeedCC(resultPresenceSeedCC* res_pres_CC);
extern void free_resultPresenceSeedCC(resultPresenceSeedCC* res_pres_CC, int free_res_pres_CC_or_not);
extern void free_resultPresenceSeed(resultPresenceSeed* res_seed, int free_res_seed_or_not);

extern int suffix_matching(CC* cc, int position, uint8_t mask, uint8_t suffix, uint8_t size_suffix_bit);

extern void sum_up_duos(Duo* d1, Duo* d2);

/* ---------------------------------------------------------------------------------------------------------------
*  presenceKmer(node, kmer, size_kmer, nb_CC_node, info_per_lvl)
*  ---------------------------------------------------------------------------------------------------------------
*  Search for a prefix into a node.
*  ---------------------------------------------------------------------------------------------------------------
*  node: pointer on a Node structure
*  kmer: pointer on the prefix to search
*  size_kmer: size of the prefix, in char.
*  nb_CC_node: number of CCs in this node
*  info_per_lvl: ptr on info_per_level structure, contains information to manipulate CCs field CC->children_type
*  ---------------------------------------------------------------------------------------------------------------
*/
void presenceNeighborsLeft(Node* restrict node, Root* root, uint8_t* restrict kmer, int size_kmer, info_per_level* restrict info_per_lvl,
                           resultPresence* res, uint16_t** skip_node_root){

    if (size_kmer == 0) ERROR("presenceNeighborsLeft(): this case should not happen")
    ASSERT_NULL_PTR(node,"presenceNeighborsLeft()")
    ASSERT_NULL_PTR(kmer,"presenceNeighborsLeft()")

    uint16_t size_bf;

    uint8_t substring[SIZE_BYTES_SUF_PREF];
    uint8_t nuc2add;
    uint8_t type;

    uint8_t s;
    uint8_t p;

    uint8_t word_tmp;

    int size_filter2;
    int size_filter2_n_skip;
    int skip_filter2;
    int skip_posfilter2;
    int posFilter2;

    //Allocate and initialize the structure resultPresence
    int last_count_node = -1;
    int cpt_node_tmp = -1;

    int a, i, j, k, m, sum;
    int cnt;
    int nb_elem;
    int hamming_weight;

    int nb_cell_3rdlist;
    int nb_skp;
    int pos_extra_filter3;
    int hamming_weight_0;

    //Keep in resultPresence->substring only the prefix we are looking for
    if (info_per_lvl->root == 1) substring[0] = reverse_word_8(kmer[0]) & 0x3f;
    else substring[0] = reverse_word_8(kmer[0]);

    substring[1] = reverse_word_8(kmer[1]);
    substring[2] = reverse_word_8(kmer[2]) & 0xc0;

    //Compute hash-functions of the Bloom filter for the prefix
    uint16_t substring_prefix = ((substring[0] & 0x3f) << SIZE_BITS_UINT_8T) | substring[1];
    uint16_t hash1_v = root->hash_v[substring_prefix * 2] % info_per_lvl->modulo_hash;
    uint16_t hash2_v = root->hash_v[substring_prefix * 2 + 1] % info_per_lvl->modulo_hash;

    substring_prefix = (substring[0] << SIZE_BITS_UINT_8T) | substring[1];

    CC* cc;
    UC* uc;

    for (i=0; i<4; i++) initialize_resultPresence(&(res[i]));

    //We first iterate on the CCs
    i = -1;
    if ((CC*)node->CC_array != NULL){
        do {
            i++;
            cc = &(((CC*)node->CC_array)[i]);

            //First look if the prefix is in the Bloom filter
            if ((cc->BF_filter2[hash1_v/SIZE_BITS_UINT_8T] & MASK_POWER_8[hash1_v%SIZE_BITS_UINT_8T]) == 0) continue;
            if ((cc->BF_filter2[hash2_v/SIZE_BITS_UINT_8T] & MASK_POWER_8[hash2_v%SIZE_BITS_UINT_8T]) == 0) continue;

            substring[0] = substring_prefix >> 6;
            substring[1] = (substring_prefix << 2) | (substring[2] >> 6);
            substring[2] = (substring_prefix >> 8) & 0xc0;

            type = (cc->type >> 6) & 0x1;

            //At this point, this prefix is said present by the BF, maybe it's a True Positive, maybe it's a False Positive
            //If it is a FP, the prefix will have to be inserted in this CC (FP recycling)
            size_bf = cc->type >> 7;
            s = (cc->type >> 1) & 0x1f; //length v of the prefixes stored in this CC (p_vs)
            p = NB_CHAR_SUF_PREF*2-s; //length u of the prefixes stored in this CC (p_us)

            size_filter2 = size_bf+(MASK_POWER_16[p]/SIZE_BITS_UINT_8T); //Size BF + filter2 in bytes
            size_filter2_n_skip = size_filter2; //Size BF + filter2 + SkipFilter2 (SkipFilter2 may not exist) in bytes
            skip_filter2 = MASK_POWER_16[p]/info_per_lvl->nb_bits_per_cell_skip_filter2;
            if (cc->nb_elem >= info_per_lvl->tresh_suf_pref) size_filter2_n_skip += skip_filter2;

            //Compute p_u, the first u char. of the prefix p we are looking for
            if (p==10) posFilter2 = (((uint16_t)substring[0]) << 2) | (((uint16_t)substring[1]) >> 6);
            else posFilter2 = (((uint16_t)substring[0]) << 6) | (((uint16_t)substring[1]) >> 2);

            //If the prefix is present in filter2, need to compute the Hamming weight between position 0 and the position of p_u
            //in order to know how many p_u are lexicographically inferior or equal to the one we insert
            if ((cc->BF_filter2[size_bf+posFilter2/SIZE_BITS_UINT_8T] & MASK_POWER_8[posFilter2%SIZE_BITS_UINT_8T]) != 0){

                k=size_bf+posFilter2/SIZE_BITS_UINT_8T;
                cnt = 0;
                hamming_weight = 0;

                //If SkipFilter2 exists, we use it to accelerate the computation of the Hamming weight
                /*if (cc->nb_elem >= info_per_lvl->tresh_suf_pref){
                    int skip_posfilter2 = posFilter2/info_per_lvl->nb_bits_per_cell_skip_filter2;

                    while ((cnt<skip_posfilter2) && (cnt < skip_filter2)){
                        hamming_weight += cc->BF_filter2[size_filter2+cnt];
                        cnt++;
                    }
                }

                //finish the computation of the Hamming weight
                hamming_weight += popcnt_8_par(cc->BF_filter2, size_bf + cnt * info_per_lvl->nb_bytes_per_cell_skip_filter2, k);

                word_tmp = cc->BF_filter2[k];
                for (k=0; k<=posFilter2 % SIZE_BITS_UINT_8T; k++)
                    if ((word_tmp & MASK_POWER_8[k]) != 0) hamming_weight++;*/

                if (cc->nb_elem >= info_per_lvl->tresh_suf_pref){

                    skip_posfilter2 = MIN(posFilter2/info_per_lvl->nb_bits_per_cell_skip_filter2, skip_filter2) + size_filter2;
                    cnt = size_filter2;

                    while (cnt < skip_posfilter2){
                        hamming_weight += cc->BF_filter2[cnt];
                        cnt++;
                    }

                    cnt -= size_filter2;
                }

                //finish the computation of the Hamming weight
                hamming_weight += popcnt_8_par(cc->BF_filter2, size_bf + cnt * info_per_lvl->nb_bytes_per_cell_skip_filter2, k);

                word_tmp = cc->BF_filter2[k];
                for (k=0; k <= posFilter2%SIZE_BITS_UINT_8T; k++, word_tmp >>= 1) hamming_weight += word_tmp & 1;

                posFilter2 = hamming_weight; //posFilter2 now contains this Hamming weight

                //At this point, we know that the p_v we are looking for should be situated in the third filter
                //at a slot (plus subsequent one) corresponding to the hamming_weight-th 1 in the extra_filter3
                j=0, k=0, m=0, sum=0;
                hamming_weight_0 = 0;
                hamming_weight = 0;
                pos_extra_filter3 = INT_MAX;
                nb_skp = CEIL(cc->nb_elem, info_per_lvl->nb_ucs_skp);
                nb_cell_3rdlist = CEIL(cc->nb_elem,SIZE_BITS_UINT_8T);

                //This loop tries to use SkipFilter3 (if it exists) to accelerate the Hamming weight comput.
                //in extra_filter3 by skipping counting 1s directly in extra_filter3
                /*while(m<cc->nb_elem / info_per_lvl->nb_bits_per_cell_skip_filter3){
                    if ((sum = hamming_weight + cc->BF_filter2[size_filter2_n_skip+m]) < posFilter2){
                        hamming_weight = sum;
                        m++;
                    }
                    else break;
                }*/

                while ((m < cc->nb_elem / info_per_lvl->nb_bits_per_cell_skip_filter3)
                       && ((hamming_weight += cc->BF_filter2[size_filter2_n_skip+m]) < posFilter2)) m++;

                if (hamming_weight >= posFilter2) hamming_weight -= cc->BF_filter2[size_filter2_n_skip+m];

                //Finish the Hamming weight computation directly in extra_filter3
                if (info_per_lvl->level_min == 1){
                    for (k = m * info_per_lvl->nb_bytes_per_cell_skip_filter3; k<nb_cell_3rdlist; k++){

                        if ((sum = hamming_weight+popcnt_8(cc->extra_filter3[k])) >= posFilter2){
                            word_tmp = cc->extra_filter3[k];

                            int size_word = SIZE_BITS_UINT_8T-1;
                            if (k == nb_cell_3rdlist-1) size_word = (cc->nb_elem-1) % SIZE_BITS_UINT_8T;

                            for (j=0; j<=size_word; j++, word_tmp >>= 1){
                                hamming_weight += word_tmp & 1;
                                if (hamming_weight == posFilter2){
                                    pos_extra_filter3 = k*SIZE_BITS_UINT_8T+j;
                                    j++;
                                    break;
                                }
                            }

                            if (k == nb_cell_3rdlist) k--;
                            goto MATCH;
                        }
                        else hamming_weight = sum;
                    }
                    //If the substring must be inserted in the last slot of the list
                    if (k == nb_cell_3rdlist) k--;

                    MATCH: if (pos_extra_filter3 == INT_MAX) pos_extra_filter3 = cc->nb_elem;

                    for (a=k; a<nb_cell_3rdlist; a++){
                        word_tmp = cc->extra_filter3[a];
                        while (j<SIZE_BITS_UINT_8T){
                            if (((word_tmp >> j)&1) == 0){
                                if (a*SIZE_BITS_UINT_8T+j < cc->nb_elem) hamming_weight_0++;
                                else goto OUT_LOOP;
                            }
                            else goto OUT_LOOP;
                            j++;
                        }
                        j=0;
                    }
                }
                else{
                    int size_line_children;
                    int pos_children;
                    int cpt_pv;
                    int cpt_node;
                    int nb_elem_in_pv;
                    int end;
                    int it;

                    uint8_t* children;

                    k = m * info_per_lvl->nb_bits_per_cell_skip_filter3;
                    pos_children = k/info_per_lvl->nb_ucs_skp;

                    //count_Nodes_Children(cc, pos_children * info_per_lvl->nb_ucs_skp, k, &cpt_pv, &cpt_node, type);
                    //last_count_node = (cpt_node += count_nodes(cc, 0, pos_children * info_per_lvl->nb_ucs_skp, type));
                    nb_elem_in_pv = pos_children * info_per_lvl->nb_ucs_skp;
                    count_Nodes_Children(cc, nb_elem_in_pv, k, &cpt_pv, &cpt_node, type);

                    if (nb_elem_in_pv > cc->nb_elem - nb_elem_in_pv){
                        last_count_node = (cpt_node += cc->nb_Node_children - count_nodes(cc, nb_elem_in_pv, cc->nb_elem, type));
                    }
                    else last_count_node = (cpt_node += count_nodes(cc, 0, nb_elem_in_pv, type));

                    it = k - pos_children * info_per_lvl->nb_ucs_skp;
                    nb_elem_in_pv = 0;

                    while (pos_children < nb_skp){

                        if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
                        else end = info_per_lvl->nb_ucs_skp;

                        uc = &(((UC*)cc->children)[pos_children]);
                        size_line_children = info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot;
                        children = &(uc->suffixes[info_per_lvl->size_kmer_in_bytes_minus_1-1]);

                        while (it < end){

                            if ((nb_elem_in_pv = getNbElts(cc, k, type)) == 0){
                                hamming_weight += cc->children_Node_container[cpt_node].UC_array.nb_children & 0x1;
                                cpt_node++;
                            }
                            else{
                                hamming_weight += children[cpt_pv*size_line_children] >> 7;
                                cpt_pv += nb_elem_in_pv;
                            }

                            if (hamming_weight == posFilter2){
                                pos_extra_filter3 = k;
                                goto MATCH2;
                            }

                            it++;
                            k++;
                        }

                        it = 0;
                        cpt_pv = 0;
                        pos_children++;
                    }

                    MATCH2: if (pos_extra_filter3 == INT_MAX) pos_extra_filter3 = cc->nb_elem;

                    cpt_node_tmp = cpt_node;

                    if (pos_children < nb_skp){

                        if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
                        else end = info_per_lvl->nb_ucs_skp;

                        it++;
                        k++;

                        if (it >= end){
                            it = 0;
                            cpt_pv = 0;
                            pos_children++;
                        }

                        while (pos_children < nb_skp){

                            uc = &(((UC*)cc->children)[pos_children]);
                            size_line_children = info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot;
                            children = &(uc->suffixes[info_per_lvl->size_kmer_in_bytes_minus_1-1]);

                            if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
                            else end = info_per_lvl->nb_ucs_skp;

                            while (it < end){

                                if ((nb_elem_in_pv = getNbElts(cc, k, type)) == 0){
                                    if (IS_EVEN(cc->children_Node_container[cpt_node].UC_array.nb_children & 0x1)) hamming_weight_0++;
                                    else goto OUT_LOOP;
                                    cpt_node++;
                                }
                                else{
                                    if (IS_EVEN(children[cpt_pv*size_line_children] >> 7)) hamming_weight_0++;
                                    else goto OUT_LOOP;
                                    cpt_pv += nb_elem_in_pv;
                                }

                                k++;
                                it++;
                            }

                            it = 0;
                            cpt_pv = 0;
                            pos_children++;
                        }
                    }
                }

                //We now compare the p_vs between start position and end position in filter3 to see if
                //the prefix we look for is present
                OUT_LOOP:if (pos_extra_filter3 < cc->nb_elem){
                    uint8_t suffix;
                    uint8_t tmp;

                    if (s==8){ //if the length of p_vs is 8bits
                        suffix = (substring[1] << 2) | (substring[2] >> 6); //compute p_v

                        if (info_per_lvl->root == 1){
                            suffix &= 0xfc;
                            uint8_t next_suffix = suffix | 0x3;
                            int last_k = 0;
                            int last_node = 0;
                            int clust;

                            for (k=pos_extra_filter3; k<=pos_extra_filter3+hamming_weight_0; k++){ //iterate between start and end position
                                if((cc->filter3[k] & 0xfc) == suffix){ //if there is a match

                                    nuc2add = cc->filter3[k] & 0x3;
                                    res[nuc2add].bucket = k/info_per_lvl->nb_ucs_skp;
                                    clust = k/SIZE_CLUST_SKIP_NODES;
                                    uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                    if (size_kmer != NB_CHAR_SUF_PREF){
                                        if (is_child(cc, k, type)){

                                            res[nuc2add].pos_sub_bucket = res[nuc2add].bucket * info_per_lvl->nb_ucs_skp;
                                            nb_elem = MIN(cc->nb_elem - res[nuc2add].pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                            if (k - res[nuc2add].pos_sub_bucket < res->pos_sub_bucket + nb_elem - k){
                                                res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);
                                            }
                                            else res[nuc2add].pos_sub_bucket = uc->nb_children - count_children(cc, k, res[nuc2add].pos_sub_bucket + nb_elem, type);
                                            //res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);

                                            res[nuc2add].pos_extra_filter3 = k;
                                            res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket*(info_per_lvl->size_kmer_in_bytes_minus_1+uc->size_annot)]);
                                            res[nuc2add].container = &(((CC*)node->CC_array)[i]);
                                            res[nuc2add].children_type_leaf = 1;
                                        }
                                        else{
                                            //res->link_child = &(cc->children_Node_container[count_nodes((CC*)&(((CC*)node->CC_array)[i]), 0, k)]);
                                            if (clust-1 >= 0){
                                                if ((skip_node_root != NULL) && (k-last_k > k-clust*SIZE_CLUST_SKIP_NODES)){
                                                    last_node = skip_node_root[i][clust-1] +
                                                                count_nodes(cc, clust*SIZE_CLUST_SKIP_NODES, k, type);
                                                }
                                                else last_node += count_nodes(cc, last_k, k, type);
                                            }
                                            else last_node += count_nodes(cc, last_k, k, type);

                                            res[nuc2add].link_child = &(cc->children_Node_container[last_node]);
                                            last_k = k;
                                        }
                                    }
                                    else{
                                        res[nuc2add].pos_extra_filter3 = k;
                                        res[nuc2add].pos_sub_bucket = k % info_per_lvl->nb_ucs_skp;
                                        res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket*uc->size_annot]);
                                        res[nuc2add].container = uc;
                                        res[nuc2add].posFilter2 = 0;
                                        res[nuc2add].posFilter3 = MIN(info_per_lvl->nb_ucs_skp, cc->nb_elem - res[nuc2add].bucket * info_per_lvl->nb_ucs_skp);
                                        res[nuc2add].children_type_leaf = 1;
                                    }
                                }
                                else if (cc->filter3[k] > next_suffix) break;
                            }
                        }
                        else{
                            //for (k=pos_extra_filter3; k<=pos_extra_filter3+hamming_weight_0; k++){ //iterate between start and end position

                                int imid;
                                int imin = pos_extra_filter3;
                                int imax = pos_extra_filter3+hamming_weight_0;

                                while (imin < imax){
                                    imid = imin + (imax-imin)/2;

                                    if (cc->filter3[imid] < suffix) imin = imid + 1;
                                    else imax = imid;
                                }

                                k = imin;

                                if (cc->filter3[k] == suffix){ //if there is a match
                                    nuc2add = cc->filter3[k] & 0x3;

                                    if (size_kmer != NB_CHAR_SUF_PREF){
                                        if (is_child(cc, k, type) == 1){

                                            res[nuc2add].bucket = k/info_per_lvl->nb_ucs_skp;
                                            uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                            res[nuc2add].pos_extra_filter3 = k;

                                            res[nuc2add].pos_sub_bucket = res[nuc2add].bucket * info_per_lvl->nb_ucs_skp;
                                            nb_elem = MIN(cc->nb_elem - res[nuc2add].pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                            if (k - res[nuc2add].pos_sub_bucket < res->pos_sub_bucket + nb_elem - k){
                                                res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);
                                            }
                                            else res[nuc2add].pos_sub_bucket = uc->nb_children - count_children(cc, k, res[nuc2add].pos_sub_bucket + nb_elem, type);
                                            //res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);

                                            res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket * (info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot)]);
                                            res[nuc2add].container = cc;
                                            res[nuc2add].children_type_leaf = 1;
                                        }
                                        else{
                                            //if (last_count_node == -1) res[nuc2add].link_child = &(cc->children_Node_container[count_nodes(cc, 0, k, type)]);
                                            if (last_count_node == -1){
                                                if (k < cc->nb_elem - k) res[nuc2add].link_child = &(cc->children_Node_container[count_nodes(cc, 0, k, type)]);
                                                else res[nuc2add].link_child = &(cc->children_Node_container[cc->nb_Node_children - count_nodes(cc, k, cc->nb_elem, type)]);
                                            }
                                            else res[nuc2add].link_child = &(cc->children_Node_container[cpt_node_tmp
                                                                             + count_nodes(cc, pos_extra_filter3+1, k+1, type) - 1]);
                                        }
                                    }
                                    else{
                                        res[nuc2add].bucket = k/info_per_lvl->nb_ucs_skp;
                                        uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                        res[nuc2add].pos_extra_filter3 = k;
                                        res[nuc2add].pos_sub_bucket = k%info_per_lvl->nb_ucs_skp;
                                        res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket * uc->size_annot]);
                                        res[nuc2add].container = uc;
                                        res[nuc2add].posFilter2 = 0;
                                        res[nuc2add].posFilter3 = MIN(info_per_lvl->nb_ucs_skp, cc->nb_elem - res[nuc2add].bucket * info_per_lvl->nb_ucs_skp);
                                        res[nuc2add].children_type_leaf = 1;
                                    }

                                    //return;
                                }
                                //else if(cc->filter3[k] > suffix) break;
                            //}
                        }
                    }
                    else {
                        suffix = ((substring[1] & 0x3) << 2) | (substring[2] >> 6);

                        if (info_per_lvl->root == 1){

                            suffix &= 0xfc;
                            uint8_t next_suffix = suffix | 0x3;
                            int last_k = 0;
                            int last_node = 0;
                            int clust;

                            for (k=pos_extra_filter3; k<=pos_extra_filter3+hamming_weight_0; k++){

                                if (IS_ODD(k)) tmp = cc->filter3[k/2] >> 4;
                                else tmp = cc->filter3[k/2] & 0xf;

                                if((tmp & 0xfc) == suffix){
                                    nuc2add = tmp & 0x3;
                                    res[nuc2add].bucket = k/info_per_lvl->nb_ucs_skp;
                                    clust = k/SIZE_CLUST_SKIP_NODES;
                                    uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                    if (size_kmer != NB_CHAR_SUF_PREF){
                                        if (is_child(cc, k, type)){

                                            res[nuc2add].pos_sub_bucket = res[nuc2add].bucket * info_per_lvl->nb_ucs_skp;
                                            nb_elem = MIN(cc->nb_elem - res[nuc2add].pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                            if (k - res[nuc2add].pos_sub_bucket < res->pos_sub_bucket + nb_elem - k){
                                                res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);
                                            }
                                            else res[nuc2add].pos_sub_bucket = uc->nb_children - count_children(cc, k, res[nuc2add].pos_sub_bucket + nb_elem, type);
                                            //res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);

                                            res[nuc2add].pos_extra_filter3 = k;
                                            res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket*(info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot)]);
                                            res[nuc2add].container = cc;
                                            res[nuc2add].children_type_leaf = 1;
                                        }
                                        else{
                                            //res->link_child = &(cc->children_Node_container[count_nodes(&(((CC*)node->CC_array)[i]), 0, k)]);
                                            if (clust-1 >= 0){
                                                if ((skip_node_root != NULL) && (k-last_k > k-clust*SIZE_CLUST_SKIP_NODES)){
                                                    last_node = skip_node_root[i][clust-1] + count_nodes(cc, clust*SIZE_CLUST_SKIP_NODES, k, type);
                                                }
                                                else last_node += count_nodes(cc, last_k, k, type);
                                            }
                                            else last_node += count_nodes(cc, last_k, k, type);

                                            res[nuc2add].link_child = &(cc->children_Node_container[last_node]);
                                            last_k = k;
                                        }
                                    }
                                    else{
                                        res[nuc2add].pos_extra_filter3 = k;
                                        res[nuc2add].pos_sub_bucket = k%info_per_lvl->nb_ucs_skp;
                                        res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket * uc->size_annot]);
                                        res[nuc2add].container = uc;
                                        res[nuc2add].posFilter2 = 0;
                                        res[nuc2add].posFilter3 = MIN(info_per_lvl->nb_ucs_skp, cc->nb_elem - res[nuc2add].bucket * info_per_lvl->nb_ucs_skp);
                                        res[nuc2add].children_type_leaf = 1;
                                    }
                                }
                                else if(tmp > next_suffix) break;
                            }
                        }
                        else{
                            //for (k=pos_extra_filter3; k<=pos_extra_filter3+hamming_weight_0; k++){

                            //    if (IS_ODD(k)) tmp = cc->filter3[k/2] >> 4;
                            //    else tmp = cc->filter3[k/2] & 0xf;

                                int imid;
                                int imin = pos_extra_filter3;
                                int imax = pos_extra_filter3+hamming_weight_0;

                                while (imin < imax){
                                    imid = imin + (imax-imin)/2;

                                    if (IS_ODD(imid)) tmp = cc->filter3[imid/2] >> 4;
                                    else tmp = cc->filter3[imid/2] & 15;

                                    if (tmp < suffix) imin = imid + 1;
                                    else imax = imid;
                                }

                                if (IS_ODD(imin)) tmp = cc->filter3[imin/2] >> 4;
                                else tmp = cc->filter3[imin/2] & 15;

                                k = imin;

                                if(tmp == suffix){
                                    nuc2add = tmp & 0x3;

                                    if (size_kmer != NB_CHAR_SUF_PREF){
                                        if (is_child(cc, k, type)){

                                            res[nuc2add].bucket = k/info_per_lvl->nb_ucs_skp;
                                            uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                            res[nuc2add].pos_extra_filter3 = k;

                                            res[nuc2add].pos_sub_bucket = res[nuc2add].bucket * info_per_lvl->nb_ucs_skp;
                                            nb_elem = MIN(cc->nb_elem - res[nuc2add].pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                            if (k - res[nuc2add].pos_sub_bucket < res->pos_sub_bucket + nb_elem - k){
                                                res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);
                                            }
                                            else res[nuc2add].pos_sub_bucket = uc->nb_children - count_children(cc, k, res[nuc2add].pos_sub_bucket + nb_elem, type);
                                            //res[nuc2add].pos_sub_bucket = count_children(cc, (res[nuc2add].bucket) * info_per_lvl->nb_ucs_skp, k, type);

                                            res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket*(info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot)]);
                                            res[nuc2add].container = cc;
                                            res[nuc2add].children_type_leaf = 1;
                                        }
                                        else{
                                            if (last_count_node == -1){
                                                //res[nuc2add].link_child = &(cc->children_Node_container[count_nodes(cc, 0, k, type)]);
                                                if (k < cc->nb_elem - k) res[nuc2add].link_child = &(cc->children_Node_container[count_nodes(cc, 0, k, type)]);
                                                else res[nuc2add].link_child = &(cc->children_Node_container[cc->nb_Node_children - count_nodes(cc, k, cc->nb_elem, type)]);
                                            }
                                            else res[nuc2add].link_child = &(cc->children_Node_container[cpt_node_tmp
                                                                             + count_nodes(cc, pos_extra_filter3+1, k+1, type) - 1]);
                                        }
                                    }
                                    else{
                                        res[nuc2add].bucket = k/info_per_lvl->nb_ucs_skp;
                                        uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                        res[nuc2add].pos_extra_filter3 = k;
                                        res[nuc2add].pos_sub_bucket = k%info_per_lvl->nb_ucs_skp;
                                        res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket * uc->size_annot]);
                                        res[nuc2add].container = uc;
                                        res[nuc2add].posFilter2 = 0;
                                        res[nuc2add].posFilter3 = MIN(info_per_lvl->nb_ucs_skp, cc->nb_elem - res[nuc2add].bucket * info_per_lvl->nb_ucs_skp);
                                        res[nuc2add].children_type_leaf = 1;
                                    }

                                    //return;
                                }
                                //else if(tmp > suffix) break;
                            //}
                        }
                    }
                }

                return;
            }
            else return;
        }
        while ((((CC*)node->CC_array)[i].type & 0x1) == 0);
    }

    //If the prefix was not found in any CC Bloom filters, we search in the UC of the node
    if (node->UC_array.suffixes != NULL){
        int nb_cell = info_per_lvl->size_kmer_in_bytes;
        int size_line = nb_cell+node->UC_array.size_annot;

        int k;

        if (info_per_lvl->root == 1){
            i = 0;
            for (k=0; k<(node->UC_array.nb_children >> 1)*size_line; k+=size_line){ //Iterate over every suffix stored
                if (memcmp(&(node->UC_array.suffixes[k+1]), &(kmer[1]), (nb_cell-1)*sizeof(uint8_t)) == 0){
                    if ((node->UC_array.suffixes[k] & 0xfc) == (kmer[0] & 0xfc)){

                        nuc2add = node->UC_array.suffixes[k] & 0x3;
                        res[nuc2add].link_child = &(node->UC_array.suffixes[k]);
                        res[nuc2add].pos_sub_bucket = k/size_line;
                        res[nuc2add].container = &(node->UC_array);
                        res[nuc2add].posFilter2 = nb_cell;
                        res[nuc2add].posFilter3 = node->UC_array.nb_children >> 1;
                        res[nuc2add].container_is_UC = 1;
                        i++;
                        if (i == 4) return;
                    }
                }
            }
        }
        else{
            /*for (k=0; k<(node->UC_array.nb_children >> 1)*size_line; k+=size_line){ //Iterate over every suffix stored
                if (memcmp(&(node->UC_array.suffixes[k]), kmer, nb_cell) == 0){

                    nuc2add = node->UC_array.suffixes[k] & 0x3;
                    res[nuc2add].link_child = &(node->UC_array.suffixes[k]);
                    res[nuc2add].pos_sub_bucket = k/size_line;
                    res[nuc2add].container = &(node->UC_array);
                    res[nuc2add].posFilter2 = nb_cell;
                    res[nuc2add].posFilter3 = node->UC_array.nb_children >> 1;
                    res[nuc2add].container_is_UC = 1;
                    return;
                }
            }*/

            int pos = binary_search_UC(&(node->UC_array), 0, (node->UC_array.nb_children >> 1) - 1, kmer, nb_cell, 0xff);

            if (memcmp(&(node->UC_array.suffixes[pos * size_line]), kmer, nb_cell * sizeof(uint8_t)) == 0){
                nuc2add = node->UC_array.suffixes[pos * size_line] & 0x3;
                res[nuc2add].link_child = &(node->UC_array.suffixes[pos * size_line]);
                res[nuc2add].pos_sub_bucket = pos;
                res[nuc2add].container = &(node->UC_array);
                res[nuc2add].posFilter2 = nb_cell;
                res[nuc2add].posFilter3 = node->UC_array.nb_children >> 1;
                res[nuc2add].container_is_UC = 1;
            }
        }
    }

    return;
}

/* ---------------------------------------------------------------------------------------------------------------
*  presenceKmer(node, kmer, size_kmer, nb_CC_node, info_per_lvl)
*  ---------------------------------------------------------------------------------------------------------------
*  Search for a prefix into a node.
*  ---------------------------------------------------------------------------------------------------------------
*  node: pointer on a Node structure
*  kmer: pointer on the prefix to search
*  size_kmer: size of the prefix, in char.
*  nb_CC_node: number of CCs in this node
*  info_per_lvl: ptr on info_per_level structure, contains information to manipulate CCs field CC->children_type
*  ---------------------------------------------------------------------------------------------------------------
*/
void presenceNeighborsRight(Node* restrict node, Root* root, uint8_t* restrict kmer, int size_kmer, info_per_level* restrict info_per_lvl,
                            resultPresence* res, uint16_t** skip_node_root){

    if (size_kmer == 0) ERROR("presenceNeighborsRight(): this case should not happen")
    ASSERT_NULL_PTR(node,"presenceNeighborsRight()")
    ASSERT_NULL_PTR(kmer,"presenceNeighborsRight()")

    uint16_t size_bf;
    uint8_t s;
    uint8_t p;
    uint8_t type;

    uint8_t substring[3];
    uint8_t word_tmp;
    uint8_t nuc2add;

    //Allocate and initialize the structure resultPresence
    int last_count_node = -1;
    int cpt_node_tmp = -1;

    int cnt;
    int hamming_weight;

    int a, i, j, k, m, sum;
    int nb_elem;
    int size_filter2;
    int size_filter2_n_skip;
    int skip_filter2;
    int skip_posfilter2;
    int posFilter2;

    int nb_skp;
    int pos_extra_filter3;
    int hamming_weight_0;
    int nb_cell_3rdlist;

    CC* cc;
    UC* uc;

    for (i=0; i<4; i++) initialize_resultPresence(&(res[i]));

    //Keep in resultPresence->substring only the prefix we are looking for
    if (size_kmer == NB_CHAR_SUF_PREF){
        substring[0] = reverse_word_8(kmer[0]);
        substring[1] = reverse_word_8(kmer[1]) & 0xfc;
        substring[2] = reverse_word_8(kmer[2]) & 0xc0;
    }
    else{
        substring[0] = reverse_word_8(kmer[0]);
        substring[1] = reverse_word_8(kmer[1]);
        substring[2] = reverse_word_8(kmer[2]) & 0xc0;
    }

    //Compute hash-functions of the Bloom filter for the prefix
    uint16_t substring_prefix = (substring[0] << SIZE_BITS_UINT_8T) | substring[1];
    uint16_t hash1_v = root->hash_v[(substring_prefix & 0x3fff) * 2] % info_per_lvl->modulo_hash;
    uint16_t hash2_v = root->hash_v[(substring_prefix & 0x3fff) * 2 + 1] % info_per_lvl->modulo_hash;

    //We first iterate on the CCs
    i = -1;

    if ((CC*)node->CC_array != NULL){
        do {
            i++;
            cc = &(((CC*)node->CC_array)[i]);

            //First look if the prefix is in the Bloom filter
            if ((cc->BF_filter2[hash1_v/SIZE_BITS_UINT_8T] & MASK_POWER_8[hash1_v%SIZE_BITS_UINT_8T]) == 0) continue;
            if ((cc->BF_filter2[hash2_v/SIZE_BITS_UINT_8T] & MASK_POWER_8[hash2_v%SIZE_BITS_UINT_8T]) == 0) continue;

            substring[0] = substring_prefix >> 6;
            substring[1] = (substring_prefix << 2) | (substring[2] >> 6);
            substring[2] = (substring_prefix >> 8) & 0xc0;

            size_bf = cc->type >> 7;
            type = (cc->type >> 6) & 0x1;
            s = (cc->type >> 1) & 0x1f; //length v of the prefixes stored in this CC (p_vs)
            p = NB_CHAR_SUF_PREF*2-s; //length u of the prefixes stored in this CC (p_us)

            size_filter2 = size_bf+(MASK_POWER_16[p]/SIZE_BITS_UINT_8T); //Size BF + filter2 in bytes
            size_filter2_n_skip = size_filter2; //Size BF + filter2 + SkipFilter2 (SkipFilter2 may not exist) in bytes
            skip_filter2 = MASK_POWER_16[p]/info_per_lvl->nb_bits_per_cell_skip_filter2;
            if (cc->nb_elem >= info_per_lvl->tresh_suf_pref) size_filter2_n_skip += skip_filter2;

            //Compute p_u, the first u char. of the prefix p we are looking for
            if (p==10) posFilter2 = (((uint16_t)substring[0]) << 2) | (((uint16_t)substring[1]) >> 6);
            else posFilter2 = (((uint16_t)substring[0]) << 6) | (((uint16_t)substring[1]) >> 2);

            //If the prefix is present in filter2, need to compute the Hamming weight between position 0 and the position of p_u
            //in order to know how many p_u are lexicographically inferior or equal to the one we insert
            if ((cc->BF_filter2[size_bf+posFilter2/SIZE_BITS_UINT_8T] & MASK_POWER_8[posFilter2%SIZE_BITS_UINT_8T]) != 0){

                k=size_bf+posFilter2/SIZE_BITS_UINT_8T;
                cnt = 0;
                hamming_weight = 0;

                //If SkipFilter2 exists, we use it to accelerate the computation of the Hamming weight
                /*if (cc->nb_elem >= info_per_lvl->tresh_suf_pref){
                    int skip_posfilter2 = posFilter2/info_per_lvl->nb_bits_per_cell_skip_filter2;

                    while ((cnt<skip_posfilter2) && (cnt < skip_filter2)){
                        hamming_weight += cc->BF_filter2[size_filter2+cnt];
                        cnt++;
                    }
                }*/

                if (cc->nb_elem >= info_per_lvl->tresh_suf_pref){

                    skip_posfilter2 = MIN(posFilter2/info_per_lvl->nb_bits_per_cell_skip_filter2, skip_filter2) + size_filter2;
                    cnt = size_filter2;

                    while (cnt < skip_posfilter2){
                        hamming_weight += cc->BF_filter2[cnt];
                        cnt++;
                    }

                    cnt -= size_filter2;
                }

                //finish the computation of the Hamming weight
                hamming_weight += popcnt_8_par(cc->BF_filter2, size_bf + cnt * info_per_lvl->nb_bytes_per_cell_skip_filter2, k);

                word_tmp = cc->BF_filter2[k];
                for (k=0; k <= posFilter2%SIZE_BITS_UINT_8T; k++, word_tmp >>= 1) hamming_weight += word_tmp & 1;

                posFilter2 = hamming_weight; //posFilter2 now contains this Hamming weight

                //At this point, we know that the p_v we are looking for should be situated in the third filter
                //at a slot (plus subsequent one) corresponding to the hamming_weight-th 1 in the extra_filter3
                j=0, m=0;
                hamming_weight = 0;
                hamming_weight_0 = 0;
                pos_extra_filter3 = INT_MAX;
                nb_skp = CEIL(cc->nb_elem, info_per_lvl->nb_ucs_skp);
                nb_cell_3rdlist = CEIL(cc->nb_elem,SIZE_BITS_UINT_8T);

                //This loop tries to use SkipFilter3 (if it exists) to accelerate the Hamming weight comput.
                //in extra_filter3 by skipping counting 1s directly in extra_filter3
                while ((m < cc->nb_elem / info_per_lvl->nb_bits_per_cell_skip_filter3)
                       && ((hamming_weight += cc->BF_filter2[size_filter2_n_skip+m]) < posFilter2)) m++;

                if (hamming_weight >= posFilter2) hamming_weight -= cc->BF_filter2[size_filter2_n_skip+m];

                //Finish the Hamming weight computation directly in extra_filter3
                if (info_per_lvl->level_min == 1){
                    for (k = m * info_per_lvl->nb_bytes_per_cell_skip_filter3; k<nb_cell_3rdlist; k++){

                        if ((sum = hamming_weight+popcnt_8(cc->extra_filter3[k])) >= posFilter2){
                            word_tmp = cc->extra_filter3[k];

                            int size_word = SIZE_BITS_UINT_8T-1;
                            if (k == nb_cell_3rdlist-1) size_word = (cc->nb_elem-1) % SIZE_BITS_UINT_8T;

                            for (j=0; j<=size_word; j++, word_tmp >>= 1){
                                hamming_weight += word_tmp & 1;
                                if (hamming_weight == posFilter2){
                                    pos_extra_filter3 = k*SIZE_BITS_UINT_8T+j;
                                    j++;
                                    break;
                                }
                            }

                            if (k == nb_cell_3rdlist) k--;
                            goto MATCH;
                        }
                        else hamming_weight = sum;
                    }
                    //If the substring must be inserted in the last slot of the list
                    if (k == nb_cell_3rdlist) k--;

                    MATCH: if (pos_extra_filter3 == INT_MAX) pos_extra_filter3 = cc->nb_elem;

                    for (a=k; a<nb_cell_3rdlist; a++){
                        word_tmp = cc->extra_filter3[a];
                        while (j<SIZE_BITS_UINT_8T){
                            if (((word_tmp >> j)&1) == 0){
                                if (a*SIZE_BITS_UINT_8T+j < cc->nb_elem) hamming_weight_0++;
                                else goto OUT_LOOP;
                            }
                            else goto OUT_LOOP;
                            j++;
                        }
                        j=0;
                    }
                }
                else{
                    int size_line_children;
                    int pos_children;
                    int cpt_pv;
                    int cpt_node;
                    int nb_elem_in_pv;
                    int end;
                    int it;

                    uint8_t* children;

                    k = m * info_per_lvl->nb_bits_per_cell_skip_filter3;
                    pos_children = k/info_per_lvl->nb_ucs_skp;

                    //count_Nodes_Children(cc, pos_children * info_per_lvl->nb_ucs_skp, k, &cpt_pv, &cpt_node, type);
                    //last_count_node = (cpt_node += count_nodes(cc, 0, pos_children * info_per_lvl->nb_ucs_skp, type));

                    nb_elem_in_pv = pos_children * info_per_lvl->nb_ucs_skp;
                    count_Nodes_Children(cc, nb_elem_in_pv, k, &cpt_pv, &cpt_node, type);

                    if (nb_elem_in_pv > cc->nb_elem - nb_elem_in_pv){
                        last_count_node = (cpt_node += cc->nb_Node_children - count_nodes(cc, nb_elem_in_pv, cc->nb_elem, type));
                    }
                    else last_count_node = (cpt_node += count_nodes(cc, 0, nb_elem_in_pv, type));

                    it = k - pos_children * info_per_lvl->nb_ucs_skp;
                    nb_elem_in_pv = 0;

                    while (pos_children < nb_skp){

                        if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
                        else end = info_per_lvl->nb_ucs_skp;

                        uc = &(((UC*)cc->children)[pos_children]);
                        size_line_children = info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot;
                        children = &(uc->suffixes[info_per_lvl->size_kmer_in_bytes_minus_1-1]);

                        while (it < end){

                            if ((nb_elem_in_pv = getNbElts(cc, k, type)) == 0){
                                hamming_weight += cc->children_Node_container[cpt_node].UC_array.nb_children & 0x1;
                                cpt_node++;
                            }
                            else{
                                hamming_weight += children[cpt_pv*size_line_children] >> 7;
                                cpt_pv += nb_elem_in_pv;
                            }

                            if (hamming_weight == posFilter2){
                                pos_extra_filter3 = k;
                                goto MATCH2;
                            }

                            it++;
                            k++;
                        }

                        it = 0;
                        cpt_pv = 0;
                        pos_children++;
                    }

                    MATCH2: if (pos_extra_filter3 == INT_MAX) pos_extra_filter3 = cc->nb_elem;

                    cpt_node_tmp = cpt_node;

                    if (pos_children < nb_skp){

                        if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
                        else end = info_per_lvl->nb_ucs_skp;

                        it++;
                        k++;

                        if (it >= end){
                            it = 0;
                            cpt_pv = 0;
                            pos_children++;
                        }

                        while (pos_children < nb_skp){

                            uc = &(((UC*)cc->children)[pos_children]);
                            size_line_children = info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot;
                            children = &(uc->suffixes[info_per_lvl->size_kmer_in_bytes_minus_1-1]);

                            if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
                            else end = info_per_lvl->nb_ucs_skp;

                            while (it < end){

                                if ((nb_elem_in_pv = getNbElts(cc, k, type)) == 0){
                                    if ((cc->children_Node_container[cpt_node].UC_array.nb_children & 0x1) == 0) hamming_weight_0++;
                                    else goto OUT_LOOP;
                                    cpt_node++;
                                }
                                else{
                                    if ((children[cpt_pv*size_line_children] >> 7) == 0) hamming_weight_0++;
                                    else goto OUT_LOOP;
                                    cpt_pv += nb_elem_in_pv;
                                }

                                k++;
                                it++;
                            }

                            it = 0;
                            cpt_pv = 0;
                            pos_children++;
                        }
                    }
                }

                //We now compare the p_vs between start position and end position in filter3 to see if
                //the prefix we look for is present
                OUT_LOOP:if (pos_extra_filter3 < cc->nb_elem){
                    uint8_t suffix;
                    uint8_t tmp;

                    if (s==8){ //if the length of p_vs is 8bits
                        suffix = (substring[1] << 2) | (substring[2] >> 6); //compute p_v

                        if (size_kmer == NB_CHAR_SUF_PREF){

                            uint8_t next_suffix = suffix | 0xc;

                            for (k=pos_extra_filter3; k<=pos_extra_filter3+hamming_weight_0; k++){ //iterate between start and end position

                                if((cc->filter3[k] & 0xf3) == suffix){ //if there is a match

                                    nuc2add = (cc->filter3[k] >> 2) & 0x3;
                                    res[nuc2add].bucket = k / info_per_lvl->nb_ucs_skp;
                                    uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                    res[nuc2add].pos_sub_bucket = k % info_per_lvl->nb_ucs_skp;
                                    res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket * uc->size_annot]);
                                    res[nuc2add].container = uc;
                                    res[nuc2add].posFilter2 = 0;
                                    res[nuc2add].posFilter3 = MIN(info_per_lvl->nb_ucs_skp, cc->nb_elem - res[nuc2add].bucket * info_per_lvl->nb_ucs_skp);
                                }
                                else if(cc->filter3[k] > next_suffix) break;
                            }
                        }
                        else{
                            for (k=pos_extra_filter3; k<=pos_extra_filter3+hamming_weight_0; k++){ //iterate between start and end position

                                if(cc->filter3[k] == suffix){ //if there is a match

                                    nuc2add = cc->filter3[k] & 0x3;

                                    if (is_child(cc, k, type) == 1){

                                        res[nuc2add].bucket = k / info_per_lvl->nb_ucs_skp;
                                        uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                        res[nuc2add].pos_extra_filter3 = k;

                                        res[nuc2add].pos_sub_bucket = res[nuc2add].bucket * info_per_lvl->nb_ucs_skp;
                                        nb_elem = MIN(cc->nb_elem - res[nuc2add].pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                        if (k - res[nuc2add].pos_sub_bucket < res->pos_sub_bucket + nb_elem - k){
                                            res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);
                                        }
                                        else res[nuc2add].pos_sub_bucket = uc->nb_children - count_children(cc, k, res[nuc2add].pos_sub_bucket + nb_elem, type);
                                        //res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);

                                        res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket*(info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot)]);
                                        res[nuc2add].container = cc;
                                        res[nuc2add].children_type_leaf = 1;
                                    }
                                    else{
                                        if ((skip_node_root != NULL) && (info_per_lvl->root == 1) && (last_count_node == -1)){

                                            int clust = k/SIZE_CLUST_SKIP_NODES;

                                            if (clust-1 >= 0){
                                                last_count_node = skip_node_root[i][clust-1] + count_nodes(cc, clust*SIZE_CLUST_SKIP_NODES, k, type);
                                            }
                                            //else last_count_node = count_nodes(cc, 0, k, type);
                                            else if (k < cc->nb_elem - k) last_count_node = count_nodes(cc, 0, k, type);
                                            else last_count_node = cc->nb_Node_children - count_nodes(cc, k, cc->nb_elem, type);

                                            res[nuc2add].link_child = &(cc->children_Node_container[last_count_node]);
                                        }
                                        else{
                                            if (last_count_node == -1){
                                                //res[nuc2add].link_child = &(cc->children_Node_container[count_nodes(cc, 0, k, type)]);
                                                if (k < cc->nb_elem - k) res[nuc2add].link_child = &(cc->children_Node_container[count_nodes(cc, 0, k, type)]);
                                                else res[nuc2add].link_child = &(cc->children_Node_container[cc->nb_Node_children - count_nodes(cc, k, cc->nb_elem, type)]);
                                            }
                                            else res[nuc2add].link_child = &(cc->children_Node_container[cpt_node_tmp +
                                                                             count_nodes(cc, pos_extra_filter3+1, k+1, type) - 1]);

                                            if (cc->nb_Node_children == 0) ERROR("presenceKmer(s=8): the count of nodes in this CC is 0, but some nodes are detected\n");
                                        }
                                    }

                                    return;
                                }
                                else if(cc->filter3[k] > suffix) break;
                            }
                        }
                    }
                    else {
                        suffix = ((substring[1] & 0x3) << 2) | (substring[2] >> 6);
                        if (size_kmer == NB_CHAR_SUF_PREF){

                            uint8_t next_suffix = suffix | 0xc;

                            for (k=pos_extra_filter3; k<=pos_extra_filter3+hamming_weight_0; k++){

                                if (IS_ODD(k)) tmp = cc->filter3[k/2] >> 4;
                                else tmp = cc->filter3[k/2] & 0xf;

                                if((tmp & 0x3) == suffix){

                                    nuc2add = (tmp >> 2) & 0x3;
                                    res[nuc2add].bucket = k / info_per_lvl->nb_ucs_skp;
                                    uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                    res[nuc2add].pos_sub_bucket = k % info_per_lvl->nb_ucs_skp;
                                    res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket * uc->size_annot]);
                                    res[nuc2add].container = uc;
                                    res[nuc2add].posFilter2 = 0;
                                    res[nuc2add].posFilter3 = MIN(info_per_lvl->nb_ucs_skp, cc->nb_elem - res[nuc2add].bucket * info_per_lvl->nb_ucs_skp);
                                }
                                else if(tmp > next_suffix) break;
                            }
                        }
                        else{
                            for (k=pos_extra_filter3; k<=pos_extra_filter3+hamming_weight_0; k++){

                                if (IS_ODD(k)) tmp = cc->filter3[k/2] >> 4;
                                else tmp = cc->filter3[k/2] & 0xf;

                                if(tmp == suffix){

                                    nuc2add = tmp & 0x3;
                                    if (is_child(cc, k, type)){

                                        res[nuc2add].bucket = k / info_per_lvl->nb_ucs_skp;
                                        uc = &(((UC*)cc->children)[res[nuc2add].bucket]);

                                        res[nuc2add].pos_extra_filter3 = k;

                                        res[nuc2add].pos_sub_bucket = res[nuc2add].bucket * info_per_lvl->nb_ucs_skp;
                                        nb_elem = MIN(cc->nb_elem - res[nuc2add].pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                        if (k - res[nuc2add].pos_sub_bucket < res->pos_sub_bucket + nb_elem - k){
                                            res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);
                                        }
                                        else res[nuc2add].pos_sub_bucket = uc->nb_children - count_children(cc, k, res[nuc2add].pos_sub_bucket + nb_elem, type);
                                        //res[nuc2add].pos_sub_bucket = count_children(cc, res[nuc2add].bucket * info_per_lvl->nb_ucs_skp, k, type);

                                        res[nuc2add].link_child = &(uc->suffixes[res[nuc2add].pos_sub_bucket*(info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot)]);
                                        res[nuc2add].container = cc;
                                        res[nuc2add].children_type_leaf = 1;
                                    }
                                    else{
                                        if ((skip_node_root != NULL) && (info_per_lvl->root == 1) && (last_count_node == -1)){

                                            int clust = k/SIZE_CLUST_SKIP_NODES;

                                            if (clust-1 >= 0){
                                                last_count_node = skip_node_root[i][clust-1] +
                                                                    count_nodes(cc, clust*SIZE_CLUST_SKIP_NODES, k, type);
                                            }
                                            //else last_count_node = count_nodes(cc, 0, k, type);
                                            else if (k < cc->nb_elem - k) last_count_node = count_nodes(cc, 0, k, type);
                                            else last_count_node = cc->nb_Node_children - count_nodes(cc, k, cc->nb_elem, type);

                                            res[nuc2add].link_child = &(cc->children_Node_container[last_count_node]);
                                        }
                                        else{
                                            if (last_count_node == -1){
                                                if (k < cc->nb_elem - k) res[nuc2add].link_child = &(cc->children_Node_container[count_nodes(cc, 0, k, type)]);
                                                else res[nuc2add].link_child = &(cc->children_Node_container[cc->nb_Node_children - count_nodes(cc, k, cc->nb_elem, type)]);
                                            }
                                            else res[nuc2add].link_child = &(cc->children_Node_container[cpt_node_tmp +
                                                                             count_nodes(cc, pos_extra_filter3+1, k+1, type) - 1]);

                                            if (cc->nb_Node_children == 0) ERROR("presenceKmer(s=8): the count of nodes in this CC is 0, but some nodes are detected\n");
                                        }
                                    }

                                    return;
                                }
                                else if(tmp > suffix) break;
                            }
                        }
                    }
                }

                return;
            }
            else return;
        }
        while ((((CC*)node->CC_array)[i].type & 0x1) == 0);
    }

    //If the prefix was not found in any CC Bloom filters, we search in the UC of the node
    if (node->UC_array.suffixes != NULL){
        int nb_cell = info_per_lvl->size_kmer_in_bytes;
        int size_line = nb_cell+node->UC_array.size_annot;

        int k;
        uint8_t bits_left = (size_kmer*2)%SIZE_BITS_UINT_8T;
        uint8_t mask2 = MASK_POWER_8[bits_left] - MASK_POWER_8[bits_left-2];
        uint8_t mask = info_per_lvl->mask_shift_kmer;

        if (mask == 0xff) mask = 0;

        i = 0;
        bits_left -= 2;

        for (k=0; k<(node->UC_array.nb_children >> 1)*size_line; k+=size_line){ //Iterate over every suffix stored

            if (memcmp(&(node->UC_array.suffixes[k]), kmer, (nb_cell-1)*sizeof(uint8_t)) == 0){

                if ((node->UC_array.suffixes[k+nb_cell-1] & mask) == kmer[nb_cell-1]){

                    nuc2add = (node->UC_array.suffixes[k+nb_cell-1] & mask2) >> bits_left;
                    res[nuc2add].link_child = &(node->UC_array.suffixes[k]);
                    res[nuc2add].container = &(node->UC_array);
                    res[nuc2add].posFilter2 = nb_cell;
                    res[nuc2add].posFilter3 = node->UC_array.nb_children >> 1;
                    res[nuc2add].pos_sub_bucket = k/size_line;
                    res[nuc2add].container_is_UC = 1;
                    i++;
                    if (i == 4) return;

                }
            }
        }
    }

    return;
}

/* ---------------------------------------------------------------------------------------------------------------
*  presenceKmer(node, kmer, size_kmer, nb_CC_node, info_per_lvl)
*  ---------------------------------------------------------------------------------------------------------------
*  Search for a prefix into a node.
*  ---------------------------------------------------------------------------------------------------------------
*  node: pointer on a Node structure
*  kmer: pointer on the prefix to search
*  size_kmer: size of the prefix, in char.
*  nb_CC_node: number of CCs in this node
*  info_per_lvl: ptr on info_per_level structure, contains information to manipulate CCs field CC->children_type
*  ---------------------------------------------------------------------------------------------------------------
*/
void presenceKmer(Node* restrict node, Root* root, uint8_t* restrict kmer, int size_kmer, int posCC_start_search,
                  int verify_UC_or_not, info_per_level* restrict info_per_lvl, resultPresence* res){

    if (size_kmer == 0) ERROR("presenceKmer(): this case should not happen")
    ASSERT_NULL_PTR(node,"presenceKmer()")
    ASSERT_NULL_PTR(kmer,"presenceKmer()")

    int i = posCC_start_search-1;

    int posFilter2;
    int nb_elem;
    int cpt_node_tmp;
    int pos_extra_filter3;
    int hamming_weight_0;

    int imid, imin, imax;

    CC* cc;
    UC* uc;

    uint8_t type;
    uint8_t s, p;
    uint8_t suffix, tmp;

    uint16_t hash1_v;
    uint16_t hash2_v;
    uint16_t hash1_v_mod;
    uint16_t hash2_v_mod;

    uint16_t size_bf;

    uint32_t substring_prefix;

    //Allocate and initialize the structure resultPresence
    initialize_resultPresence(res);

    res->node = node;
    res->level_node = size_kmer/NB_CHAR_SUF_PREF - 1;

    //Keep in resultPresence->substring only the prefix we are looking for
    res->substring[0] = reverse_word_8(kmer[0]);
    res->substring[1] = reverse_word_8(kmer[1]);
    res->substring[2] = reverse_word_8(kmer[2]) & 0xc0;

    //Compute hash-functions of the Bloom filter for the prefix

    if (root->compressed > 0){
        substring_prefix = (res->substring[0] << 10) | (res->substring[1] << 2) | (res->substring[2] >> 6);
        hash1_v = root->hash_v[substring_prefix * 2] % info_per_lvl->modulo_hash;
        hash2_v = root->hash_v[substring_prefix * 2 + 1] % info_per_lvl->modulo_hash;
    }
    else{
        substring_prefix = (res->substring[0] << SIZE_BITS_UINT_8T) | res->substring[1];
        hash1_v = root->hash_v[(substring_prefix & 0x3fff) * 2] % info_per_lvl->modulo_hash;
        hash2_v = root->hash_v[(substring_prefix & 0x3fff) * 2 + 1] % info_per_lvl->modulo_hash;
    }

    hash1_v_mod = hash1_v%SIZE_BITS_UINT_8T;
    hash2_v_mod = hash2_v%SIZE_BITS_UINT_8T;

    hash1_v /= SIZE_BITS_UINT_8T;
    hash2_v /= SIZE_BITS_UINT_8T;

    //We first iterate on the CCs
    if (node->CC_array != NULL){
        do {
            i++;
            cc = &(((CC*)node->CC_array)[i]);

            res->pos_container = i;

            //First look if the prefix is in the Bloom filter
            //if ((cc->BF_filter2[hash1_v/SIZE_BITS_UINT_8T] & MASK_POWER_8[hash1_v%SIZE_BITS_UINT_8T]) == 0) continue;
            //if ((cc->BF_filter2[hash2_v/SIZE_BITS_UINT_8T] & MASK_POWER_8[hash2_v%SIZE_BITS_UINT_8T]) == 0) continue;

            if ((cc->BF_filter2[hash1_v] & MASK_POWER_8[hash1_v_mod]) == 0) continue;
            if ((cc->BF_filter2[hash2_v] & MASK_POWER_8[hash2_v_mod]) == 0) continue;

            res->container = cc;
            res->presBF = 1;

            if (root->compressed <= 0){
                res->substring[0] = (substring_prefix >> 6) & 0xff;
                res->substring[1] = (substring_prefix << 2) | (res->substring[2] >> 6);
                res->substring[2] = (substring_prefix >> 8) & 0xc0;
            }

            //At this point, this prefix is said present by the BF, maybe it's a True Positive, maybe it's a False Positive
            //If it is a FP, the prefix will have to be inserted in this CC (FP recycling)
            size_bf = cc->type >> 7;
            type = (cc->type >> 6) & 0x1;
            s = (cc->type >> 1) & 0x1f; //length v of the prefixes stored in this CC (p_vs)
            p = NB_CHAR_SUF_PREF*2-s; //length u of the prefixes stored in this CC (p_us)

            //Compute p_u, the first u char. of the prefix p we are looking for
            if (p==10) posFilter2 = (((uint16_t)res->substring[0]) << 2) | (((uint16_t)res->substring[1]) >> 6);
            else posFilter2 = (((uint16_t)res->substring[0]) << 6) | (((uint16_t)res->substring[1]) >> 2);

            //If the prefix is present in filter2, need to compute the Hamming weight between position 0 and the position of p_u
            //in order to know how many p_u are lexicographically inferior or equal to the one we insert
            if ((cc->BF_filter2[size_bf+posFilter2/SIZE_BITS_UINT_8T] & MASK_POWER_8[posFilter2%SIZE_BITS_UINT_8T]) != 0){

                findCluster(cc, posFilter2, &cpt_node_tmp, &pos_extra_filter3, &hamming_weight_0, res, info_per_lvl);

                res->pos_extra_filter3 = pos_extra_filter3;

                //We now compare the p_vs between start position and end position in filter3 to see if
                //the prefix we look for is present
                if (pos_extra_filter3 < cc->nb_elem){

                    imin = pos_extra_filter3;
                    imax = pos_extra_filter3+hamming_weight_0;

                    if (s==8){ //if the length of p_vs is 8bits

                        suffix = (res->substring[1] << 2) | (res->substring[2] >> 6); //compute p_v

                        while (imin < imax){
                            imid = (imin + imax) / 2;

                            if (cc->filter3[imid] < suffix) imin = imid + 1;
                            else imax = imid;
                        }

                        if(cc->filter3[imin] == suffix){ //if there is a match

                            res->presFilter3 = 1;
                            res->posFilter3 = imin;

                            if (size_kmer != NB_CHAR_SUF_PREF){ //if the current CC is not a leaf

                                __builtin_prefetch(cc->children_type, 0, 2);

                                if (is_child(cc, imin, type) == 1){

                                    res->bucket = imin / info_per_lvl->nb_ucs_skp;

                                    uc = &(((UC*)cc->children)[res->bucket]);

                                    /*res->pos_sub_bucket = res->bucket * info_per_lvl->nb_ucs_skp;
                                    nb_elem = MIN(cc->nb_elem - res->pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                    if (imin - res->pos_sub_bucket > res->pos_sub_bucket + nb_elem - imin){
                                        res->pos_sub_bucket = uc->nb_children - count_children(cc, imin, res->pos_sub_bucket + nb_elem, type);
                                    }
                                    else res->pos_sub_bucket = count_children(cc, res->pos_sub_bucket, imin, type);*/


                                    res->pos_sub_bucket = res->bucket * info_per_lvl->nb_ucs_skp;
                                    nb_elem = MIN(cc->nb_elem - res->pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                    if ((info_per_lvl->level_min == 0) && (res->pos_children == res->bucket)){
                                        if (imin - pos_extra_filter3 > res->pos_sub_bucket + nb_elem - imin){
                                            res->pos_sub_bucket = uc->nb_children - count_children(cc, imin, res->pos_sub_bucket + nb_elem, type);
                                        }
                                        else res->pos_sub_bucket = res->count_children + count_children(cc, pos_extra_filter3, imin, type);
                                    }
                                    else if (imin - res->pos_sub_bucket > res->pos_sub_bucket + nb_elem - imin){
                                        res->pos_sub_bucket = uc->nb_children - count_children(cc, imin, res->pos_sub_bucket + nb_elem, type);
                                    }
                                    else res->pos_sub_bucket = count_children(cc, res->pos_sub_bucket, imin, type);

                                    res->link_child = &(uc->suffixes[res->pos_sub_bucket* (info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot)]);
                                    res->children_type_leaf = 1;
                                }
                                else{
                                    if (cpt_node_tmp == -1){
                                        //res->link_child = &(cc->children_Node_container[count_nodes(cc, 0, k)]);
                                        if (imin < cc->nb_elem - imin) res->link_child = &(cc->children_Node_container[count_nodes(cc, 0, imin, type)]);
                                        else res->link_child = &(cc->children_Node_container[cc->nb_Node_children - count_nodes(cc, imin, cc->nb_elem, type)]);
                                    }
                                    else res->link_child = &(cc->children_Node_container[res->count_nodes +
                                                                                        count_nodes(cc, pos_extra_filter3, imin+1, type) - 1]);

                                    if (cc->nb_Node_children == 0) ERROR( "presenceKmer(s=8): the count of nodes in this CC is 0, but some nodes are detected" )
                                }
                            }
                            else{
                                res->bucket = imin/info_per_lvl->nb_ucs_skp;
                                res->pos_sub_bucket = imin%info_per_lvl->nb_ucs_skp;

                                uc = &(((UC*)cc->children)[res->bucket]);

                                res->link_child = &(uc->suffixes[res->pos_sub_bucket * uc->size_annot]);

                                res->posFilter2 = 0;
                                res->posFilter3 = MIN(info_per_lvl->nb_ucs_skp, cc->nb_elem - res->bucket * info_per_lvl->nb_ucs_skp);
                            }

                            return;
                        }
                        else if(cc->filter3[imin] > suffix) res->posFilter3 = imin;
                        else res->posFilter3 = imin+1;
                    }
                    else {
                        suffix = ((res->substring[1] & 0x3) << 2) | (res->substring[2] >> 6);
                        tmp = 0;

                        while (imin < imax){
                            imid = (imin + imax) /2;

                            if (IS_ODD(imid)) tmp = cc->filter3[imid/2] >> 4;
                            else tmp = cc->filter3[imid/2] & 0xf;

                            if (tmp < suffix) imin = imid + 1;
                            else imax = imid;
                        }

                        if (IS_ODD(imin)) tmp = cc->filter3[imin/2] >> 4;
                        else tmp = cc->filter3[imin/2] & 0xf;

                        if(tmp == suffix){

                            res->presFilter3 = 1;
                            res->posFilter3 = imin;
                            //Need to determine which child
                            if (size_kmer != NB_CHAR_SUF_PREF){
                                if (is_child(cc, imin, type)){

                                    __builtin_prefetch(cc->children_type, 0, 2);

                                    res->bucket = imin/info_per_lvl->nb_ucs_skp;
                                    uc = &(((UC*)cc->children)[res->bucket]);


                                    /*res->pos_sub_bucket = res->bucket * info_per_lvl->nb_ucs_skp;
                                    nb_elem = MIN(cc->nb_elem - res->pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                    if (k - res->pos_sub_bucket > res->pos_sub_bucket + nb_elem - k){
                                        res->pos_sub_bucket = uc->nb_children - count_children(cc, k, res->pos_sub_bucket + nb_elem, type);
                                    }
                                    else res->pos_sub_bucket = count_children(cc, res->pos_sub_bucket, k, type);*/


                                    res->pos_sub_bucket = res->bucket * info_per_lvl->nb_ucs_skp;
                                    nb_elem = MIN(cc->nb_elem - res->pos_sub_bucket, info_per_lvl->nb_ucs_skp);

                                    if ((info_per_lvl->level_min == 0) && (res->pos_children == res->bucket)){
                                        if (imin - pos_extra_filter3 > res->pos_sub_bucket + nb_elem - imin){
                                            res->pos_sub_bucket = uc->nb_children - count_children(cc, imin, res->pos_sub_bucket + nb_elem, type);
                                        }
                                        else res->pos_sub_bucket = res->count_children + count_children(cc, pos_extra_filter3, imin, type);
                                    }
                                    else if (imin - res->pos_sub_bucket > res->pos_sub_bucket + nb_elem - imin){
                                        res->pos_sub_bucket = uc->nb_children - count_children(cc, imin, res->pos_sub_bucket + nb_elem, type);
                                    }
                                    else res->pos_sub_bucket = count_children(cc, res->pos_sub_bucket, imin, type);

                                    res->link_child = &(uc->suffixes[res->pos_sub_bucket * (info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot)]);
                                    res->children_type_leaf = 1;
                                }
                                else{
                                    if (cpt_node_tmp == -1){
                                        //res->link_child = &(cc->children_Node_container[count_nodes(cc, 0, k)]);
                                        if (imin < cc->nb_elem - imin) res->link_child = &(cc->children_Node_container[count_nodes(cc, 0, imin, type)]);
                                        else res->link_child = &(cc->children_Node_container[cc->nb_Node_children - count_nodes(cc, imin, cc->nb_elem, type)]);
                                    }
                                    else res->link_child = &(cc->children_Node_container[res->count_nodes + count_nodes(cc, pos_extra_filter3, imin+1, type) - 1]);

                                    if (cc->nb_Node_children == 0) ERROR ("presenceKmer(s=4): the count of nodes in this CC is 0, but some nodes are detected")
                                }
                            }
                            else{
                                res->bucket = imin/info_per_lvl->nb_ucs_skp;
                                res->pos_sub_bucket = imin%info_per_lvl->nb_ucs_skp;

                                uc = &(((UC*)cc->children)[res->bucket]);
                                res->link_child = &(uc->suffixes[res->pos_sub_bucket * uc->size_annot]);

                                res->posFilter2 = 0;
                                res->posFilter3 = MIN(info_per_lvl->nb_ucs_skp, cc->nb_elem - res->bucket * info_per_lvl->nb_ucs_skp);
                            }

                            return;
                        }
                        else if(tmp > suffix) res->posFilter3 = imin;
                        else res->posFilter3 = imin+1;
                    }
                }

                return;
            }
            else return;
        }
        while (IS_EVEN(((CC*)node->CC_array)[i].type));
    }

    //If the prefix was not found in any CC Bloom filters, we search in the UC of the node
    if ((verify_UC_or_not != 0) && (node->UC_array.suffixes != NULL)){

        int nb_cell = info_per_lvl->size_kmer_in_bytes;
        int size_line = nb_cell + node->UC_array.size_annot;
        int cmp_pos = node->UC_array.nb_children >> 1;
        int pos = binary_search_UC(&(node->UC_array), 0, cmp_pos - 1, kmer, nb_cell, 0xff);

        cmp_pos = memcmp(&(node->UC_array.suffixes[pos * size_line]), kmer, nb_cell * sizeof(uint8_t));

        if (cmp_pos == 0){
            res->container = &(node->UC_array);
            res->link_child = &(node->UC_array.suffixes[pos * size_line]);
            res->pos_sub_bucket = pos;
            res->container_is_UC = 1;
            res->posFilter2 = nb_cell;
            res->posFilter3 = node->UC_array.nb_children >> 1;
        }
        else if (cmp_pos < 0) res->pos_sub_bucket = pos+1;
        else res->pos_sub_bucket = pos;
    }

    return;
}

void presenceSeed(Node* restrict node, Root* root, int lvl_node, uint8_t* restrict seed, int size_seed, int size_kmer,
                  int posCC_start_search, int id_genome, int search_for_id_genome, int stop_if_2idgenomes_found,
                  resultPresenceSeed* res, annotation_inform* ann_inf, annotation_array_elem* annot_sorted, Duo* d1){

    if ((size_kmer == 0) || (size_seed == 0)) ERROR("presenceSeed(): this case should not happen")

    ASSERT_NULL_PTR(node, "presenceSeed()")
    ASSERT_NULL_PTR(seed, "presenceSeed()")
    ASSERT_NULL_PTR(res, "presenceSeed()")
    ASSERT_NULL_PTR(ann_inf, "presenceSeed()")
    ASSERT_NULL_PTR(d1, "presenceSeed()")

    CC* cc;
    UC* uc;

    resultPresenceSeedCC* res_pres_ccs;

    uint8_t* annot;
    uint8_t* annot_ext;
    uint8_t* annot_cplx;

    uint8_t** annot2;
    uint8_t** annot_ext2;
    uint8_t** annot_cplx2;

    uint8_t* annot_ext_tmp;

    int* size_annot2;
    int* size_annot_cplx2;

    int j = 0, k = 0;
    int cpt_pv = 0;
    int cpt_node = 0;
    int it_filter2 = 0;
    int hamming_weight_0 = 0;

    int cpt_node_tmp = -1;
    int first_it_filter2 = -1;
    int i = posCC_start_search-1;

    int size_seed_array = CEIL(size_seed * 2, SIZE_BITS_UINT_8T);
    int shift = (SIZE_BYTES_SUF_PREF - 1) * SIZE_BITS_UINT_8T;
    int search_for_id_genome_cpy = search_for_id_genome;

    int nb_skp;
    int nb_elt;
    int first_bit;
    int it_filter3;
    int prev_it_filter3_pv;
    int prev_it_filter3_node;
    int it_children_bucket;
    int prev_it_children_bucket;
    int pos_pres_in_filter;
    int pos_extra_filter3;
    int size_line;

    int size_annot;
    int size_annot_cplx;

    uint8_t mask_end_seed = MASK_POWER_16[SIZE_BITS_UINT_8T - (size_seed_array * SIZE_BITS_UINT_8T - size_seed * 2)]-1;
    uint8_t nuc_tmp = seed[0] & 0x3;

    uint8_t s;
    uint8_t p;
    uint8_t mask;
    uint8_t suffix;
    uint8_t type;

    uint8_t substring[SIZE_BYTES_SUF_PREF];
    uint8_t seed_tmp[SIZE_BYTES_SUF_PREF];

    uint16_t size_bf;

    uint32_t seed_concat_rev = 0;

    res->n = node;

    memset(seed_tmp, 0, SIZE_BYTES_SUF_PREF * sizeof(uint8_t));
    for (j=0; j<size_seed_array; j++) seed_tmp[j] = seed[j];
    seed_tmp[size_seed_array-1] &= mask_end_seed;

    if (root->compressed > 0){
        seed_concat_rev = (reverse_word_8(seed_tmp[0]) << SIZE_BITS_UINT_8T) | reverse_word_8(seed_tmp[1]);
        seed_concat_rev = (seed_concat_rev << SIZE_BITS_UINT_8T) | reverse_word_8(seed_tmp[2]);
    }
    else{
        seed_concat_rev = (reverse_word_8(seed_tmp[0] & 0xfc) << SIZE_BITS_UINT_8T) | reverse_word_8(seed_tmp[1]);
        seed_concat_rev = (seed_concat_rev << SIZE_BITS_UINT_8T) | reverse_word_8(seed_tmp[2]);
        seed_concat_rev = (seed_concat_rev << 2) | (nuc_tmp << 6);
    }

    //Keep in resultPresence->substring only the prefix we are looking for
    substring[0] = seed_concat_rev >> shift;
    shift -= SIZE_BITS_UINT_8T;
    substring[1] = seed_concat_rev >> shift;
    shift -= SIZE_BITS_UINT_8T;
    substring[2] = seed_concat_rev >> shift;

    //We first iterate on the CCs
    if ((CC*)node->CC_array != NULL){
        do {
            i++;

            cpt_pv = 0;
            cpt_node = -1;
            cpt_node_tmp = -1;
            first_it_filter2 = -1;
            prev_it_children_bucket = -1;
            pos_extra_filter3 = INT_MAX;

            res->res_pres_ccs = realloc(res->res_pres_ccs, (i+1) * sizeof(resultPresenceSeedCC));
            res_pres_ccs = &(res->res_pres_ccs[i]);
            res->nb_res_pres_ccs++;

            init_resultPresenceSeedCC(res_pres_ccs);

            cc = &(((CC*)node->CC_array)[i]);

            nb_skp = CEIL(cc->nb_elem, root->info_per_lvl[lvl_node].nb_ucs_skp);

            size_bf = cc->type >> 7;
            s = (cc->type >> 1) & 0x1f;
            p = NB_CHAR_SUF_PREF*2-s;
            type = (cc->type >> 6) & 0x1;

            if (s==8){ //if the length of p_vs is 8bits

                int nb_bits_used = (size_seed - 1) * 2 - p;

                mask = 0;
                if (nb_bits_used > 0) mask = 0xff - MASK_POWER_8[SIZE_BITS_UINT_8T - nb_bits_used] + 1;
                mask |= 0x3;

                suffix = (substring[1] << 2) | (substring[2] >> 6);
            }
            else{
                mask = 0x3;
                if (size_seed == NB_CHAR_SUF_PREF) mask = 0xff;

                suffix = ((substring[1] & 0x3) << 2) | (substring[2] >> 6);
            }

            if (p==10) res_pres_ccs->start_pos_filter2 = (((uint16_t)substring[0]) << 2) | (((uint16_t)substring[1]) >> 6);
            else res_pres_ccs->start_pos_filter2 = (((uint16_t)substring[0]) << 6) | (((uint16_t)substring[1]) >> 2);

            if ((size_seed - 1) * 2 >= p) res_pres_ccs->end_pos_filter2 = res_pres_ccs->start_pos_filter2;
            else res_pres_ccs->end_pos_filter2 = res_pres_ccs->start_pos_filter2 + MASK_POWER_16[p - (size_seed - 1) * 2] - 1;

            //res_pres_ccs->pres_in_filter2 = calloc(CEIL(res_pres_ccs->end_pos_filter2 - res_pres_ccs->start_pos_filter2 + 1, SIZE_BITS_UINT_8T), sizeof(uint8_t));
            //ASSERT_NULL_PTR(res_pres_ccs->pres_in_filter2, "presenceSeed()");

            it_filter2 = res_pres_ccs->start_pos_filter2;
            pos_pres_in_filter = 0;

            while (it_filter2 <= res_pres_ccs->end_pos_filter2){

                if ((cc->BF_filter2[size_bf+it_filter2/SIZE_BITS_UINT_8T] & MASK_POWER_8[it_filter2%SIZE_BITS_UINT_8T]) != 0){

                    //res_pres_ccs->pres_in_filter2[pos_pres_in_filter / SIZE_BITS_UINT_8T] |= MASK_POWER_8[pos_pres_in_filter % SIZE_BITS_UINT_8T];

                    //if (first_it_filter2 == -1){
                        first_it_filter2 = it_filter2;
                        break;
                    //}
                }

                it_filter2++;
                pos_pres_in_filter++;
            }

            it_filter2 = first_it_filter2;

            if ((it_filter2 != -1) && ((cc->BF_filter2[size_bf+it_filter2/SIZE_BITS_UINT_8T] & MASK_POWER_8[it_filter2%SIZE_BITS_UINT_8T]) != 0)){

                findCluster(cc, it_filter2, &cpt_node_tmp, &pos_extra_filter3, &hamming_weight_0, NULL, &(root->info_per_lvl[lvl_node]));
                res_pres_ccs->start_pos_filter3 = pos_extra_filter3;

                pos_pres_in_filter = 0;
                it_filter3 = pos_extra_filter3;

                //res_pres_ccs->pres_in_filter3 = calloc(CEIL(cc->nb_elem - it_filter3 + 1, SIZE_BITS_UINT_8T), sizeof(uint8_t));
                //ASSERT_NULL_PTR(res_pres_ccs->pres_in_filter3, "presenceSeed()")

                if (root->info_per_lvl[lvl_node].level_min == 1){

                    for (; it_filter2 <= res_pres_ccs->end_pos_filter2; it_filter2++){

                        if ((cc->BF_filter2[size_bf+it_filter2/SIZE_BITS_UINT_8T] & (MASK_POWER_8[it_filter2%SIZE_BITS_UINT_8T])) != 0){

                            first_bit = 1;

                            while((it_filter3 < cc->nb_elem)
                                  && (((cc->extra_filter3[it_filter3/SIZE_BITS_UINT_8T] & MASK_POWER_8[it_filter3%SIZE_BITS_UINT_8T]) == 0) || (first_bit == 1))){

                                if (size_kmer != NB_CHAR_SUF_PREF){

                                    if (suffix_matching(cc, it_filter3, mask, suffix, s) != 0){

                                        nb_elt = getNbElts(cc, it_filter3, type);

                                        //res_pres_ccs->pres_in_filter3[pos_pres_in_filter/SIZE_BITS_UINT_8T] |= MASK_POWER_8[pos_pres_in_filter%SIZE_BITS_UINT_8T];

                                        if (nb_elt == 0){

                                            if (cpt_node == -1){
                                                if (it_filter3 < cc->nb_elem - it_filter3) cpt_node = count_nodes(cc, 0, it_filter3, type);
                                                else cpt_node = cc->nb_Node_children - count_nodes(cc, it_filter3, cc->nb_elem, type);
                                            }
                                            else cpt_node += count_nodes(cc, prev_it_filter3_node, it_filter3, type);

                                            countKmers_Node(&(cc->children_Node_container[cpt_node]), lvl_node-1, size_kmer - NB_CHAR_SUF_PREF,
                                                            id_genome, search_for_id_genome_cpy, stop_if_2idgenomes_found, root->info_per_lvl, ann_inf,
                                                            annot_sorted, d1);

                                            if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)) goto END_PRESENCE_SEED;

                                            prev_it_filter3_node = it_filter3;
                                        }
                                        else{
                                            d1->elem1 += nb_elt;

                                            if (search_for_id_genome_cpy > 0){

                                                it_children_bucket = it_filter3/root->info_per_lvl[lvl_node].nb_ucs_skp;

                                                if (it_children_bucket == prev_it_children_bucket){
                                                    cpt_pv += count_children(cc, prev_it_filter3_pv, it_filter3, type);
                                                }
                                                else{
                                                    prev_it_children_bucket = it_children_bucket;
                                                    uc = &(((UC*)cc->children)[it_children_bucket]);

                                                    cpt_pv = count_children(cc, it_children_bucket * root->info_per_lvl[lvl_node].nb_ucs_skp, it_filter3, type);
                                                }

                                                get_annotations(uc, &annot2, &annot_ext2, &annot_cplx2, &size_annot2, &size_annot_cplx2,
                                                               root->info_per_lvl[lvl_node].size_kmer_in_bytes_minus_1, uc->nb_children, cpt_pv,
                                                               cpt_pv + nb_elt - 1);

                                                for (k = 0; k < nb_elt; k++){

                                                    if (annot_ext2 == NULL) annot_ext_tmp = NULL;
                                                    else annot_ext_tmp = annot_ext2[k];

                                                    if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot2[k],
                                                                                         size_annot2[k], annot_ext_tmp, 1, id_genome) == 1){
                                                        d1->elem2 = 1;
                                                        search_for_id_genome_cpy = 0;
                                                        break;
                                                    }
                                                }

                                                FREE_5PTRS(annot2, size_annot2, size_annot_cplx2, annot_ext2, annot_cplx2)
                                                prev_it_filter3_pv = it_filter3;

                                                if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)) goto END_PRESENCE_SEED;
                                            }
                                        }
                                    }
                                }
                                else{
                                    if (suffix_matching(cc, it_filter3, mask, suffix, s) != 0){
                                        //res_pres_ccs->pres_in_filter3[pos_pres_in_filter/SIZE_BITS_UINT_8T] |= MASK_POWER_8[pos_pres_in_filter%SIZE_BITS_UINT_8T];
                                        d1->elem1++;

                                        if (search_for_id_genome_cpy > 0){

                                            it_children_bucket = it_filter3/root->info_per_lvl[lvl_node].nb_ucs_skp;

                                            if (it_children_bucket != prev_it_children_bucket){
                                                prev_it_children_bucket = it_children_bucket;
                                                uc = &(((UC*)cc->children)[it_children_bucket]);

                                                if (it_children_bucket == nb_skp-1) nb_elt = cc->nb_elem - it_children_bucket * root->info_per_lvl[lvl_node].nb_ucs_skp;
                                                else nb_elt = root->info_per_lvl[lvl_node].nb_ucs_skp;
                                            }

                                            get_annotation(uc, &annot, &annot_ext, &annot_cplx, &size_annot, &size_annot_cplx,
                                                            0, nb_elt, it_filter3 % root->info_per_lvl[lvl_node].nb_ucs_skp);

                                            if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot,
                                                                                size_annot, annot_ext, 1, id_genome) == 1){
                                                d1->elem2 = 1;
                                                search_for_id_genome_cpy = 0;
                                            }

                                            if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)) goto END_PRESENCE_SEED;
                                        }
                                    }
                                }

                                it_filter3++;
                                pos_pres_in_filter++;
                                first_bit = 0;
                            }
                        }
                    }
                }
                else {

                    it_children_bucket = it_filter3/root->info_per_lvl[lvl_node].nb_ucs_skp;

                    int end = it_children_bucket * root->info_per_lvl[lvl_node].nb_ucs_skp;
                    int it = it_filter3 - it_children_bucket * root->info_per_lvl[lvl_node].nb_ucs_skp;
                    int nb_cell_children = root->info_per_lvl[lvl_node].size_kmer_in_bytes_minus_1-1;

                    count_Nodes_Children(cc, it_children_bucket * root->info_per_lvl[lvl_node].nb_ucs_skp, it_filter3, &cpt_pv, &cpt_node, type);

                    if (end < cc->nb_elem - end) cpt_node += count_nodes(cc, 0, end, type);
                    else cpt_node += cc->nb_Node_children - count_nodes(cc, end, cc->nb_elem, type);

                    for (; it_filter2 <= res_pres_ccs->end_pos_filter2; it_filter2++){

                        if ((cc->BF_filter2[size_bf+it_filter2/SIZE_BITS_UINT_8T] & (MASK_POWER_8[it_filter2%SIZE_BITS_UINT_8T])) != 0){

                            first_bit = 1;

                            while (it_children_bucket < nb_skp){

                                if (it_children_bucket == nb_skp - 1) end = cc->nb_elem - it_children_bucket * root->info_per_lvl[lvl_node].nb_ucs_skp;
                                else end = root->info_per_lvl[lvl_node].nb_ucs_skp;

                                uc = &(((UC*)cc->children)[it_children_bucket]);
                                size_line = root->info_per_lvl[lvl_node].size_kmer_in_bytes_minus_1 + uc->size_annot;

                                while (it < end){

                                    if ((nb_elt = getNbElts(cc, it_filter3, type)) == 0){

                                        if (((cc->children_Node_container[cpt_node].UC_array.nb_children & 0x1) == 0) || (first_bit == 1)){

                                            if (suffix_matching(cc, it_filter3, mask, suffix, s) != 0){

                                                //res_pres_ccs->pres_in_filter3[pos_pres_in_filter/SIZE_BITS_UINT_8T] |= MASK_POWER_8[pos_pres_in_filter%SIZE_BITS_UINT_8T];

                                                countKmers_Node(&(cc->children_Node_container[cpt_node]), lvl_node-1, size_kmer - NB_CHAR_SUF_PREF,
                                                                id_genome, search_for_id_genome_cpy, stop_if_2idgenomes_found, root->info_per_lvl, ann_inf,
                                                                annot_sorted, d1);

                                                if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)) goto END_PRESENCE_SEED;
                                            }

                                            first_bit = 0;
                                            cpt_node++;
                                        }
                                        else goto OUT_LOOP_FILTER2_IT;
                                    }
                                    else{
                                        if (((uc->suffixes[cpt_pv*size_line+nb_cell_children] >> 7) == 0)  || (first_bit == 1)){

                                            if (suffix_matching(cc, it_filter3, mask, suffix, s) != 0){

                                                //res_pres_ccs->pres_in_filter3[pos_pres_in_filter/SIZE_BITS_UINT_8T] |= MASK_POWER_8[pos_pres_in_filter%SIZE_BITS_UINT_8T];

                                                d1->elem1 += nb_elt;

                                                if (search_for_id_genome_cpy > 0){

                                                    get_annotations(uc, &annot2, &annot_ext2, &annot_cplx2, &size_annot2, &size_annot_cplx2,
                                                                   root->info_per_lvl[lvl_node].size_kmer_in_bytes_minus_1, uc->nb_children, cpt_pv,
                                                                   cpt_pv + nb_elt - 1);

                                                    for (k = 0; k < nb_elt; k++){

                                                        if (annot_ext2 == NULL) annot_ext_tmp = NULL;
                                                        else annot_ext_tmp = annot_ext2[k];

                                                        if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot2[k],
                                                                                             size_annot2[k], annot_ext_tmp, 1, id_genome) == 1){
                                                            d1->elem2 = 1;
                                                            search_for_id_genome_cpy = 0;
                                                            break;
                                                        }
                                                    }

                                                    FREE_5PTRS(annot2, size_annot2, size_annot_cplx2, annot_ext2, annot_cplx2)
                                                }

                                                if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)) goto END_PRESENCE_SEED;
                                            }

                                            first_bit = 0;
                                            cpt_pv += nb_elt;
                                        }
                                        else goto OUT_LOOP_FILTER2_IT;
                                    }

                                    it++;
                                    it_filter3++;
                                    pos_pres_in_filter++;
                                }

                                it = 0;
                                cpt_pv = 0;
                                it_children_bucket++;
                            }
                        }

                        OUT_LOOP_FILTER2_IT: continue;
                    }
                }

                res_pres_ccs->end_pos_filter3 = it_filter3 - 1;
                //res_pres_ccs->pres_in_filter3 = realloc(res_pres_ccs->pres_in_filter3,
                //                               CEIL(res_pres_ccs->end_pos_filter3 - res_pres_ccs->start_pos_filter3 + 1, SIZE_BITS_UINT_8T) * sizeof(uint8_t));
            }
        }
        while ((((CC*)node->CC_array)[i].type & 0x1) == 0);
    }

    //If the prefix was not found in any CC Bloom filters, we search in the UC of the node
    if (node->UC_array.suffixes != NULL){

        int nb_elt = node->UC_array.nb_children >> 1;
        int size_line = root->info_per_lvl[lvl_node].size_kmer_in_bytes + node->UC_array.size_annot;

        //res->pres_uc = calloc(CEIL(nb_elt, SIZE_BITS_UINT_8T), sizeof(uint8_t));
        //ASSERT_NULL_PTR(res->pres_uc, "presenceSeed()")

        for (i = 0, j = 0; i < nb_elt * size_line; i += size_line, j++){

            if ((memcmp(&(node->UC_array.suffixes[i]), seed_tmp, (size_seed_array - 1) * sizeof(uint8_t)) == 0) &&
                ((node->UC_array.suffixes[i + size_seed_array - 1] & mask_end_seed) == seed_tmp[size_seed_array-1])){
                    //res->pres_uc[j/SIZE_BITS_UINT_8T] |= MASK_POWER_8[j % SIZE_BITS_UINT_8T];
                    d1->elem1++;

                    if (search_for_id_genome_cpy > 0){
                        get_annotation(&(node->UC_array), &annot, &annot_ext, &annot_cplx,
                                       &size_annot, &size_annot_cplx, root->info_per_lvl[lvl_node].size_kmer_in_bytes,
                                       nb_elt, j);

                        if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot, size_annot,
                                                             annot_ext, 1, id_genome) == 1){
                            d1->elem2 = 1;
                            search_for_id_genome_cpy = 0;
                        }
                    }

                    if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)) break;
            }
        }
    }

    END_PRESENCE_SEED: return;
}

void isSeedPresent(Node* restrict node, Root* root, int lvl_node, uint8_t* restrict seed, int size_seed, int size_kmer,
                   int id_genome, int search_for_id_genome, int stop_if_2idgenomes_found, annotation_inform* ann_inf,
                   annotation_array_elem* annot_sorted, Duo* d1){

    ASSERT_NULL_PTR(node,"isSeedPresent() 1")
    ASSERT_NULL_PTR(seed,"isSeedPresent() 2")
    ASSERT_NULL_PTR(root,"isSeedPresent() 3")
    ASSERT_NULL_PTR(ann_inf,"isSeedPresent() 4")
    ASSERT_NULL_PTR(d1,"isSeedPresent() 5")

    int nb_cell;
    int size_line;
    int nb_cell_to_delete;

    int size_annot;
    int size_annot_cplx;

    int j = 0, k = 0;
    int search_for_id_genome_cpy = search_for_id_genome;

    int size_seed_array = CEIL(size_seed * 2, SIZE_BITS_UINT_8T);
    int size_seed_array_children = CEIL((size_seed - NB_CHAR_SUF_PREF) * 2, SIZE_BITS_UINT_8T);

    uint8_t mask_end_seed;

    uint16_t nb_elt;

    CC* cc;
    UC* uc;

    resultPresence* res;

    resultPresenceSeed* res_pres_seed;

    uint8_t* annot;
    uint8_t* annot_ext;
    uint8_t* annot_cplx;

    __builtin_prefetch (&(root->info_per_lvl[lvl_node]), 0, 0);

    uint8_t seed_tmp[root->info_per_lvl[lvl_node].size_kmer_in_bytes];
    memcpy(seed_tmp, seed, root->info_per_lvl[lvl_node].size_kmer_in_bytes);

    //We want to start the search at the beginning of the node so 4th argument is 0
    if (size_seed > NB_CHAR_SUF_PREF){

        res = malloc(sizeof(resultPresence));
        ASSERT_NULL_PTR(res,"isSeedPresent()")

        presenceKmer(node, root, seed_tmp, size_kmer, 0, 0, &(root->info_per_lvl[lvl_node]), res);

        if (res->link_child != NULL){

            nb_cell_to_delete = 2;
            if (size_kmer == 45) nb_cell_to_delete++;

            nb_cell = root->info_per_lvl[lvl_node].size_kmer_in_bytes;

            for (j=0; j < nb_cell - nb_cell_to_delete; j++){
                seed_tmp[j] = seed_tmp[j+2] >> 2;
                if (j+3 < nb_cell) seed_tmp[j] |= seed_tmp[j+3] << 6;
            }

            seed_tmp[j-1] &= root->info_per_lvl[lvl_node].mask_shift_kmer;

            if (res->children_type_leaf == 0){
                isSeedPresent((Node*)res->link_child, root, lvl_node-1, seed_tmp, size_seed - NB_CHAR_SUF_PREF, size_kmer-NB_CHAR_SUF_PREF,
                                id_genome, search_for_id_genome_cpy, stop_if_2idgenomes_found, ann_inf, annot_sorted, d1);
            }
            else{

                cc = (CC*)res->container;
                uc = &(((UC*)cc->children)[res->bucket]);

                nb_elt = getNbElts(cc, res->posFilter3, (cc->type >> 6) & 0x1);
                nb_cell = root->info_per_lvl[lvl_node].size_kmer_in_bytes_minus_1;
                size_line = nb_cell + uc->size_annot;

                mask_end_seed = MASK_POWER_16[SIZE_BITS_UINT_8T - (size_seed_array_children * SIZE_BITS_UINT_8T - (size_seed - NB_CHAR_SUF_PREF) * 2)]-1;

                for (j = res->pos_sub_bucket * size_line, k = res->pos_sub_bucket; j < (res->pos_sub_bucket + nb_elt) * size_line; j += size_line, k++){
                    if ((memcmp(&(uc->suffixes[j]), seed_tmp, (size_seed_array_children - 1) * sizeof(uint8_t)) == 0) &&
                        ((uc->suffixes[j + size_seed_array_children - 1] & mask_end_seed) == seed_tmp[size_seed_array_children - 1])){
                            d1->elem1++;

                            if (search_for_id_genome_cpy > 0){
                                get_annotation(uc, &annot, &annot_ext, &annot_cplx, &size_annot, &size_annot_cplx,
                                               nb_cell, uc->nb_children, k);

                                if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot, size_annot, annot_ext, 1, id_genome) == 1){
                                    d1->elem2 = 1;
                                    search_for_id_genome_cpy = 0;
                                }
                            }

                            if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)){
                                free(res);
                                return;
                            }
                        }
                }
            }
        }
        else if (node->UC_array.suffixes != NULL){

            mask_end_seed = MASK_POWER_16[SIZE_BITS_UINT_8T - (size_seed_array * SIZE_BITS_UINT_8T - size_seed * 2)]-1;
            size_line = root->info_per_lvl[lvl_node].size_kmer_in_bytes + node->UC_array.size_annot;
            nb_elt = node->UC_array.nb_children >> 1;

            for (j = 0, k = 0; j < (node->UC_array.nb_children >> 1) * size_line; j += size_line, k++){

                if ((memcmp(&(node->UC_array.suffixes[j]), seed_tmp, (size_seed_array - 1) * sizeof(uint8_t)) == 0) &&
                    ((node->UC_array.suffixes[j + size_seed_array - 1] & mask_end_seed) == seed_tmp[size_seed_array-1])){
                        d1->elem1++;

                        if (search_for_id_genome_cpy > 0){
                            get_annotation(&(node->UC_array), &annot, &annot_ext, &annot_cplx, &size_annot, &size_annot_cplx,
                                           root->info_per_lvl[lvl_node].size_kmer_in_bytes, nb_elt, k);

                            if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot, size_annot, annot_ext, 1, id_genome) == 1){
                                d1->elem2 = 1;
                                search_for_id_genome_cpy = 0;
                            }
                        }

                        if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)){
                            free(res);
                            return;
                        }
                }
            }
        }

        free(res);
    }
    else{

        res_pres_seed = malloc(sizeof(resultPresenceSeed));
        ASSERT_NULL_PTR(res_pres_seed, "isSeedPresent()")

        init_resultPresenceSeed(res_pres_seed);

        presenceSeed(node, root, lvl_node, seed_tmp, size_seed, size_kmer, 0, id_genome,
                    search_for_id_genome_cpy, stop_if_2idgenomes_found, res_pres_seed,
                    ann_inf, annot_sorted, d1);

        free_resultPresenceSeed(res_pres_seed, 1);
    }

    return;
}

void countKmers_Node(Node* restrict node, int lvl_node, int size_kmer, int id_genome,
                     int search_for_id_genome, int stop_if_2idgenomes_found, info_per_level* restrict info_per_lvl,
                     annotation_inform* ann_inf, annotation_array_elem* annot_sorted, Duo* d1){

    ASSERT_NULL_PTR(node,"countKmers_Node()")
    ASSERT_NULL_PTR(info_per_lvl, "countKmers_CC()")
    ASSERT_NULL_PTR(d1, "countKmers_CC()")

    int i = 0;
    int search_for_id_genome_cpy = search_for_id_genome;

    if (node->CC_array != NULL){

        while ((((CC*)node->CC_array)[i].type & 0x1) == 0){
            countKmers_CC(&(((CC*)node->CC_array)[i]), lvl_node, size_kmer, id_genome, search_for_id_genome_cpy,
                        stop_if_2idgenomes_found, info_per_lvl, ann_inf, annot_sorted, d1);

            if (d1->elem2 == 1){
                if (stop_if_2idgenomes_found > 0) goto END_COUNT_KMERS_NODE;
                search_for_id_genome_cpy = 0;
            }

            i++;
        }

        countKmers_CC(&(((CC*)node->CC_array)[i]), lvl_node, size_kmer, id_genome, search_for_id_genome_cpy,
                        stop_if_2idgenomes_found, info_per_lvl, ann_inf, annot_sorted, d1);

        if (d1->elem2 == 1){
            if (stop_if_2idgenomes_found > 0) goto END_COUNT_KMERS_NODE;
            search_for_id_genome_cpy = 0;
        }
    }

    if (node->UC_array.suffixes != NULL){

        uint8_t** annot;
        uint8_t** annot_ext;
        uint8_t** annot_cplx;

        uint8_t* annot_ext_tmp;

        int* size_annot;
        int* size_annot_cplx;

        uint16_t nb_elt = node->UC_array.nb_children >> 1;

        d1->elem1 += nb_elt;

        if (search_for_id_genome_cpy > 0){

            get_annotations(&(node->UC_array), &annot, &annot_ext, &annot_cplx, &size_annot, &size_annot_cplx,
                           info_per_lvl[lvl_node].size_kmer_in_bytes, nb_elt, 0, nb_elt-1);

            for (i=0; i<nb_elt; i++){

                if (annot_ext == NULL) annot_ext_tmp = NULL;
                else annot_ext_tmp = annot_ext[i];

                if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot[i], size_annot[i],
                                                     annot_ext_tmp, 1, id_genome) == 1){
                    d1->elem2 = 1;
                    break;
                }
            }

            FREE_5PTRS(annot, size_annot, size_annot_cplx, annot_ext, annot_cplx)
        }
    }

    END_COUNT_KMERS_NODE: return;
}

void countKmers_CC(CC* restrict cc, int lvl_cont, int size_kmer, int id_genome, int search_for_id_genome,
                   int stop_if_2idgenomes_found, info_per_level* restrict info_per_lvl,
                   annotation_inform* ann_inf, annotation_array_elem* annot_sorted, Duo* d1){

    ASSERT_NULL_PTR(cc, "countKmers_CC()")
    ASSERT_NULL_PTR(info_per_lvl, "countKmers_CC()")

    int i = 0, j = 0;
    int nb_skp = CEIL(cc->nb_elem, info_per_lvl[lvl_cont].nb_ucs_skp);
    int search_for_id_genome_cpy = search_for_id_genome;

    uint8_t** annot = NULL;
    uint8_t** annot_ext = NULL;
    uint8_t** annot_cplx = NULL;

    uint8_t* annot_ext_tmp;

    int* size_annot = NULL;
    int* size_annot_cplx = NULL;

    uint16_t nb_elt;

    UC* uc;

    if (size_kmer == NB_CHAR_SUF_PREF){

        d1->elem1 += cc->nb_elem;

        if (search_for_id_genome_cpy > 0){

            for (i = 0; i < nb_skp; i++){

                uc = &(((UC*)cc->children)[i]);

                if (i == nb_skp-1) nb_elt = cc->nb_elem - i * info_per_lvl[lvl_cont].nb_ucs_skp;
                else nb_elt = info_per_lvl[lvl_cont].nb_ucs_skp;

                get_annotations(uc, &annot, &annot_ext, &annot_cplx, &size_annot,
                                   &size_annot_cplx, 0, nb_elt, 0, nb_elt-1);

                for (j = 0; j < nb_elt; j++){

                    if (annot_ext == NULL) annot_ext_tmp = NULL;
                    else annot_ext_tmp = annot_ext[j];

                    if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot[j], size_annot[j],
                                                         annot_ext_tmp, 1, id_genome) == 1){
                        d1->elem2 = 1;
                        FREE_5PTRS(annot, size_annot, size_annot_cplx, annot_ext, annot_cplx)

                        goto END;
                    }
                }

                FREE_5PTRS(annot, size_annot, size_annot_cplx, annot_ext, annot_cplx)
            }
        }
    }
    else {

        for (i=0; i<nb_skp; i++){

            uc = &(((UC*)cc->children)[i]);
            d1->elem1 += uc->nb_children;

            if (search_for_id_genome_cpy > 0){

                get_annotations(uc, &annot, &annot_ext, &annot_cplx, &size_annot, &size_annot_cplx,
                               info_per_lvl[lvl_cont].size_kmer_in_bytes_minus_1, uc->nb_children, 0,
                               uc->nb_children-1);

                for (j = 0; j < uc->nb_children; j++){

                    if (annot_ext == NULL) annot_ext_tmp = NULL;
                    else annot_ext_tmp = annot_ext[j];

                    if (is_genome_present_from_end_annot(ann_inf, annot_sorted, annot[j], size_annot[j],
                                                         annot_ext_tmp, 1, id_genome) == 1){
                        d1->elem2 = 1;
                        search_for_id_genome_cpy = 0;

                        break;
                    }
                }

                FREE_5PTRS(annot, size_annot, size_annot_cplx, annot_ext, annot_cplx)
            }

            if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)) goto END;
        }

        for (i=0; i < cc->nb_Node_children ; i++){

            //Because we know a node here contains at least a CC with > 1 suffix
            if ((stop_if_2idgenomes_found > 0) && (d1->elem2 > 0)) goto END;

            countKmers_Node(&(cc->children_Node_container[i]), lvl_cont-1, size_kmer - NB_CHAR_SUF_PREF, id_genome,
                            search_for_id_genome_cpy, stop_if_2idgenomes_found, info_per_lvl,
                            ann_inf, annot_sorted, d1);
        }
    }

    END: return;
}

void findCluster(CC* cc, int pos_filter2, int* cpt_node_return, int* pos_extra_filter3, int* hamming_weight_0,
                resultPresence* res, info_per_level* restrict info_per_lvl){

    ASSERT_NULL_PTR(cc, "findCluster()")
    ASSERT_NULL_PTR(cpt_node_return, "findCluster()")
    ASSERT_NULL_PTR(pos_extra_filter3, "findCluster()")
    ASSERT_NULL_PTR(hamming_weight_0, "findCluster()")

    UC* uc;

    uint16_t size_bf = cc->type >> 7;

    uint8_t s = (cc->type >> 1) & 0x1f; //length v of the prefixes stored in this CC (p_vs)
    uint8_t p = NB_CHAR_SUF_PREF*2-s; //length u of the prefixes stored in this CC (p_us)
    uint8_t word_tmp = 0;
    uint8_t type = (cc->type >> 6) & 0x1;

    int size_filter2 = size_bf+(MASK_POWER_16[p]/SIZE_BITS_UINT_8T); //Size BF + filter2 in bytes
    int size_filter2_n_skip = size_filter2; //Size BF + filter2 + SkipFilter2 (SkipFilter2 may not exist) in bytes
    int skip_filter2 = MASK_POWER_16[p]/info_per_lvl->nb_bits_per_cell_skip_filter2;
    if (cc->nb_elem >= info_per_lvl->tresh_suf_pref) size_filter2_n_skip += skip_filter2;

   //At this point, we know that the p_v we are looking for should be situated in the third filter
    //at a slot (plus subsequent one) corresponding to the hamming_weight-th 1 in the extra_filter3
    int nb_skp = CEIL(cc->nb_elem, info_per_lvl->nb_ucs_skp);
    int nb_cell_3rdlist = CEIL(cc->nb_elem,SIZE_BITS_UINT_8T);

    int j=0, m=0, sum=0;
    int hamming_weight = 0;

    int k = size_bf + pos_filter2/SIZE_BITS_UINT_8T;
    int cnt = 0;

    int skip_posfilter2;

    int cpt_node_return_tmp = -1;
    int pos_extra_filter3_tmp = INT_MAX;
    int hamming_weight_0_tmp = 0;
    int posFilter2 = 0;

    /*
    *cpt_node_return = -1;
    *pos_extra_filter3 = INT_MAX;
    *hamming_weight_0 = 0;*/

    //If SkipFilter2 exists, we use it to accelerate the computation of the Hamming weight
    if (cc->nb_elem >= info_per_lvl->tresh_suf_pref){

        skip_posfilter2 = MIN(pos_filter2/info_per_lvl->nb_bits_per_cell_skip_filter2, skip_filter2) + size_filter2;
        cnt = size_filter2;

        while (cnt < skip_posfilter2){
            hamming_weight += cc->BF_filter2[cnt];
            cnt++;
        }

        cnt -= size_filter2;
    }

    //finish the computation of the Hamming weight
    hamming_weight += popcnt_8_par(cc->BF_filter2, size_bf + cnt * info_per_lvl->nb_bytes_per_cell_skip_filter2, k);

    word_tmp = cc->BF_filter2[k];
    for (k=0; k <= pos_filter2%SIZE_BITS_UINT_8T; k++, word_tmp >>= 1) hamming_weight += word_tmp & 1;

    if (res != NULL) res->presFilter2 = 1;
    posFilter2 = hamming_weight; //res->posFilter2 now contains this Hamming weight

    k=0;
    word_tmp = 0;
    hamming_weight = 0;

    //This loop tries to use SkipFilter3 (if it exists) to accelerate the Hamming weight comput.
    //in extra_filter3 by skipping counting 1s directly in extra_filter3
    /*while(m<cc->nb_elem/info_per_lvl->nb_bits_per_cell_skip_filter3){
        if ((sum = hamming_weight + cc->BF_filter2[size_filter2_n_skip+m]) < posFilter2){
            hamming_weight = sum;
            m++;
        }
        else break;
    }*/

    while ((m < cc->nb_elem / info_per_lvl->nb_bits_per_cell_skip_filter3)
           && ((hamming_weight += cc->BF_filter2[size_filter2_n_skip+m]) < posFilter2)) m++;

    if (hamming_weight >= posFilter2) hamming_weight -= cc->BF_filter2[size_filter2_n_skip+m];

    //Finish the Hamming weight computation directly in extra_filter3
    if (info_per_lvl->level_min == 1){

        for (k = m * info_per_lvl->nb_bytes_per_cell_skip_filter3; k<nb_cell_3rdlist; k++){

            if ((sum = hamming_weight+popcnt_8(cc->extra_filter3[k])) >= posFilter2){
                word_tmp = cc->extra_filter3[k];

                int size_word = SIZE_BITS_UINT_8T - 1;
                if (k == nb_cell_3rdlist-1) size_word = (cc->nb_elem-1) % SIZE_BITS_UINT_8T;

                for (j=0; j <= size_word; j++, word_tmp >>= 1){
                    if ((hamming_weight += IS_ODD(word_tmp)) == posFilter2){
                        pos_extra_filter3_tmp = k*SIZE_BITS_UINT_8T+j;
                        j++;
                        break;
                    }
                }

                if (k == nb_cell_3rdlist) k--;
                goto MATCH;
            }
            else hamming_weight = sum;
        }
        //If the substring must be inserted in the last slot of the list
        if (k == nb_cell_3rdlist) k--;

        MATCH: if (pos_extra_filter3_tmp == INT_MAX) pos_extra_filter3_tmp = cc->nb_elem;

        /*int a;
        for (a = k, sum = a * SIZE_BITS_UINT_8T + j; a < nb_cell_3rdlist; a++){

            while (j < SIZE_BITS_UINT_8T){

                if ((IS_ODD(cc->extra_filter3[a] >> j)) || (sum >= cc->nb_elem)) goto OUT_LOOP;
                else hamming_weight_0_tmp += 1;

                j++;
                sum++;
            }
            j=0;
        }*/

        for (sum = k * SIZE_BITS_UINT_8T + j, word_tmp = cc->extra_filter3[k] >> j; sum < cc->nb_elem; sum++, word_tmp >>= 1){
            if (sum%SIZE_BITS_UINT_8T == 0) word_tmp = cc->extra_filter3[sum/SIZE_BITS_UINT_8T];
            if (IS_ODD(word_tmp)) break;
            else hamming_weight_0_tmp++;
        }
    }
    else{
        int size_line_children;
        int pos_children;
        int cpt_pv;
        int cpt_node;
        int nb_elem_in_pv;
        int end;
        int it;

        uint8_t* children;

        k = m * info_per_lvl->nb_bits_per_cell_skip_filter3;
        pos_children = k/info_per_lvl->nb_ucs_skp;
        nb_elem_in_pv = pos_children * info_per_lvl->nb_ucs_skp;

        count_Nodes_Children(cc, nb_elem_in_pv, k, &cpt_pv, &cpt_node, type);

        if (nb_elem_in_pv > cc->nb_elem - nb_elem_in_pv){
            cpt_node_return_tmp = (cpt_node += cc->nb_Node_children - count_nodes(cc, nb_elem_in_pv, cc->nb_elem, type));
        }
        else cpt_node_return_tmp = (cpt_node += count_nodes(cc, 0, nb_elem_in_pv, type));

        nb_elem_in_pv = 0;
        it = k - pos_children * info_per_lvl->nb_ucs_skp;

        while (pos_children < nb_skp){

            __builtin_prefetch(&(cc->children_Node_container[cpt_node].UC_array), 0, 3);

            uc = &(((UC*)cc->children)[pos_children]);
            children = &(uc->suffixes[info_per_lvl->size_kmer_in_bytes_minus_1-1]);

            __builtin_prefetch(children, 0, 1);

            size_line_children = info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot;

            if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
            else end = info_per_lvl->nb_ucs_skp;

            while (it < end){

                if ((nb_elem_in_pv = getNbElts(cc, k, type)) == 0){
                    hamming_weight += cc->children_Node_container[cpt_node].UC_array.nb_children & 0x1;
                    cpt_node++;
                }
                else{
                    hamming_weight += children[cpt_pv*size_line_children] >> 7;
                    cpt_pv += nb_elem_in_pv;
                }

                if (hamming_weight == posFilter2){
                    pos_extra_filter3_tmp = k;
                    goto MATCH2;
                }

                k++;
                it++;
            }

            it = 0;
            cpt_pv = 0;
            pos_children++;
        }

        if (pos_extra_filter3_tmp == INT_MAX) pos_extra_filter3_tmp = cc->nb_elem;

        MATCH2: if (res != NULL){
            res->pos_children = pos_children;
            res->count_children = cpt_pv - nb_elem_in_pv;
            res->count_nodes = cpt_node - (nb_elem_in_pv == 0);
        }

        if (pos_children < nb_skp){

            it++;
            k++;

            if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
            else end = info_per_lvl->nb_ucs_skp;

            if (it >= end){
                it = 0;
                cpt_pv = 0;
                pos_children++;
            }

            while (pos_children < nb_skp){

                __builtin_prefetch(&(cc->children_Node_container[cpt_node].UC_array), 0, 3);

                uc = &(((UC*)cc->children)[pos_children]);
                children = &(uc->suffixes[info_per_lvl->size_kmer_in_bytes_minus_1-1]);

                __builtin_prefetch(children, 0, 1);

                size_line_children = info_per_lvl->size_kmer_in_bytes_minus_1 + uc->size_annot;

                if (pos_children == nb_skp - 1) end = cc->nb_elem - pos_children * info_per_lvl->nb_ucs_skp;
                else end = info_per_lvl->nb_ucs_skp;

                while (it < end){

                    if ((nb_elem_in_pv = getNbElts(cc, k, type)) == 0){
                        if (IS_ODD(cc->children_Node_container[cpt_node].UC_array.nb_children)) goto OUT_LOOP;
                        else hamming_weight_0_tmp += 1;
                        cpt_node++;
                    }
                    else{
                        if (IS_ODD(children[cpt_pv*size_line_children] >> 7)) goto OUT_LOOP;
                        else hamming_weight_0_tmp += 1;
                        cpt_pv += nb_elem_in_pv;
                    }

                    k++;
                    it++;
                }

                it = 0;
                cpt_pv = 0;
                pos_children++;
            }
        }
    }

    OUT_LOOP: *cpt_node_return = cpt_node_return_tmp;
    *pos_extra_filter3 = pos_extra_filter3_tmp;
    *hamming_weight_0 = hamming_weight_0_tmp;

    if (res != NULL) res->posFilter2 = posFilter2;

    return;
}

resultPresence* isKmerPresent(Node* restrict node, Root* root, int lvl_node, uint8_t* restrict kmer, int size_kmer){

    ASSERT_NULL_PTR(node,"isKmerPresent()")
    ASSERT_NULL_PTR(kmer,"isKmerPresent()")
    ASSERT_NULL_PTR(root,"isKmerPresent()")

    uint16_t nb_elt;

    int j=0;

    int nb_cell;
    int size_line;

    CC* cc;
    UC* uc;

    __builtin_prefetch (&(root->info_per_lvl[lvl_node]), 0, 0);

    uint8_t kmer_tmp[root->info_per_lvl[lvl_node].size_kmer_in_bytes];
    memcpy(kmer_tmp, kmer, root->info_per_lvl[lvl_node].size_kmer_in_bytes);

    resultPresence* res = malloc(sizeof(resultPresence));
    ASSERT_NULL_PTR(res,"isKmerPresent()")

    //We want to start the search at the beginning of the node so 4th argument is 0
    presenceKmer(node, root, kmer_tmp, size_kmer, 0, 1, &(root->info_per_lvl[lvl_node]), res);

    if (size_kmer == NB_CHAR_SUF_PREF) return res;
    else{

        int nb_cell_to_delete = 2;
        nb_cell = root->info_per_lvl[lvl_node].size_kmer_in_bytes;

        if (size_kmer == 45) nb_cell_to_delete++;

        for (j=0; j < nb_cell - nb_cell_to_delete; j++){
            kmer_tmp[j] = kmer_tmp[j+2] >> 2;
            if (j+3 < nb_cell) kmer_tmp[j] |= kmer_tmp[j+3] << 6;
        }

        kmer_tmp[j-1] &= root->info_per_lvl[lvl_node].mask_shift_kmer;

        if (res->link_child != NULL){

            if (res->children_type_leaf == 0){
                if (res->container_is_UC == 0){
                    resultPresence* res_tmp = isKmerPresent((Node*)res->link_child, root, lvl_node-1, kmer_tmp,
                                                            size_kmer-NB_CHAR_SUF_PREF);
                    free(res);
                    res = res_tmp;
                    return res;
                }
            }
            else{

                cc = (CC*)res->container;
                uc = &(((UC*)cc->children)[res->bucket]);

                nb_elt = getNbElts(cc, res->posFilter3, (cc->type >> 6) & 0x1);
                nb_cell = root->info_per_lvl[lvl_node].size_kmer_in_bytes_minus_1;
                size_line = nb_cell + uc->size_annot;

                res->link_child = NULL;
                res->container = NULL;

                if (nb_elt != 0){
                    if (size_kmer == 45){
                        j = binary_search_UC(uc, res->pos_sub_bucket, res->pos_sub_bucket + nb_elt - 1, kmer_tmp, nb_cell, 0xff);

                        if (memcmp(&(uc->suffixes[j * size_line]), kmer_tmp, nb_cell*sizeof(uint8_t)) == 0){
                            res->link_child = &(uc->suffixes[j * size_line]);
                            res->container = uc;
                            res->pos_sub_bucket = j;
                            res->posFilter2 = nb_cell;
                            res->posFilter3 = uc->nb_children;

                            return res;
                        }
                    }
                    else{
                        j = binary_search_UC(uc, res->pos_sub_bucket, res->pos_sub_bucket + nb_elt - 1, kmer_tmp, nb_cell, 0x7f);

                        if (memcmp(&(uc->suffixes[j * size_line]), kmer_tmp, (nb_cell-1)*sizeof(uint8_t)) == 0){
                            if ((uc->suffixes[j * size_line + nb_cell - 1] & 0x7f) == kmer_tmp[nb_cell - 1]){
                                res->link_child = &(uc->suffixes[j * size_line]);
                                res->container = uc;
                                res->pos_sub_bucket = j;
                                res->posFilter2 = nb_cell;
                                res->posFilter3 = uc->nb_children;

                                return res;
                            }
                        }
                    }
                }
            }
        }
    }

    return res;
}

void isKmerPresent_bis(Node* restrict node, Root* root, int lvl_node, uint8_t* restrict kmer, int size_kmer, resultPresence* res){

    ASSERT_NULL_PTR(node,"isKmerPresent()")
    ASSERT_NULL_PTR(kmer,"isKmerPresent()")
    ASSERT_NULL_PTR(root,"isKmerPresent()")

    uint16_t nb_elt;

    int j=0;

    int nb_cell;
    int size_line;

    CC* cc;
    UC* uc;

    __builtin_prefetch (&(root->info_per_lvl[lvl_node]), 0, 0);

    initialize_resultPresence(res);

    //We want to start the search at the beginning of the node so 4th argument is 0
    presenceKmer(node, root, kmer, size_kmer, 0, 1, &(root->info_per_lvl[lvl_node]), res);

    if (size_kmer == NB_CHAR_SUF_PREF) return;
    else{

        int nb_cell_to_delete = 2;
        nb_cell = root->info_per_lvl[lvl_node].size_kmer_in_bytes;

        if (size_kmer == 45) nb_cell_to_delete++;

        for (j=0; j < nb_cell - nb_cell_to_delete; j++){
            kmer[j] = kmer[j+2] >> 2;
            if (j+3 < nb_cell) kmer[j] |= kmer[j+3] << 6;
        }

        kmer[j-1] &= root->info_per_lvl[lvl_node].mask_shift_kmer;

        if (res->link_child != NULL){
            if (res->children_type_leaf == 0){
                if (res->container_is_UC == 0){
                    isKmerPresent_bis((Node*)res->link_child, root, lvl_node-1, kmer, size_kmer-NB_CHAR_SUF_PREF, res);
                    return;
                }
            }
            else{

                cc = (CC*)res->container;
                uc = &(((UC*)cc->children)[res->bucket]);

                nb_elt = getNbElts(cc, res->posFilter3, (cc->type >> 6) & 0x1);
                nb_cell = root->info_per_lvl[lvl_node].size_kmer_in_bytes_minus_1;
                size_line = nb_cell + uc->size_annot;

                res->link_child = NULL;
                res->container = NULL;

                if (nb_elt != 0){
                    if (size_kmer == 45){
                        j = binary_search_UC(uc, res->pos_sub_bucket, res->pos_sub_bucket + nb_elt - 1, kmer, nb_cell, 0xff);

                        if (memcmp(&(uc->suffixes[j * size_line]), kmer, nb_cell*sizeof(uint8_t)) == 0){
                            res->link_child = &(uc->suffixes[j * size_line]);
                            res->container = uc;
                            res->pos_sub_bucket = j;
                            res->posFilter2 = nb_cell;
                            res->posFilter3 = uc->nb_children;

                            return;
                        }
                    }
                    else{
                        j = binary_search_UC(uc, res->pos_sub_bucket, res->pos_sub_bucket + nb_elt - 1, kmer, nb_cell, 0x7f);

                        if (memcmp(&(uc->suffixes[j * size_line]), kmer, (nb_cell-1)*sizeof(uint8_t)) == 0){
                            if ((uc->suffixes[j * size_line + nb_cell - 1] & 0x7f) == kmer[nb_cell - 1]){
                                res->link_child = &(uc->suffixes[j * size_line]);
                                res->container = uc;
                                res->pos_sub_bucket = j;
                                res->posFilter2 = nb_cell;
                                res->posFilter3 = uc->nb_children;

                                return;
                            }
                        }
                    }
                }
            }
        }
    }

    return;
}
