#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define PRODFILE "produtos.bin"
#define ORDERFILE "pedidos.bin"
#define PROD_IDX_FILE "produtos.idx"
#define ORDER_IDX_FILE "pedidos.idx"
#define FATOR_INDICE 10 //indice vai armazenar 1 registro a cada 10
// Tamanho de uma página de disco (ex:4096 bytes valor universal)
#define TAMANHO_PAGINA 4096
// Tamanho aproximado de cada elemento (Chave ID 8 bytes + Ponteiro 8 bytes)
#define TAMANHO_REGISTRO_ARVORE 16 
// A ordem é calculada: quantos registros cabem em uma página?
#define ORDEM_M (TAMANHO_PAGINA / TAMANHO_REGISTRO_ARVORE) //aproximadamente ordem 256
#define MAX_CHAVES (ORDEM_M - 1)    // chaves máximas em qualquer nó
#define MIN_CHAVES (MAX_CHAVES / 2) 

typedef struct idx_record {
    long long id;      
    long long offset;   
} Indice;

typedef struct prod
{
    long long id;
    char categoria[20];
    float preco;
} Produto;

typedef struct ped{
    long long id;
    long long id_produto;
    char data[11];
} Pedido;

//structs arvoreB+
typedef struct {
    long long id;
    long long offset; // posição no arquivo produtos.bin
} PonteiroRegistro;

typedef struct NoBMais {
    bool eh_folha;
    int num_chaves;
    long long chaves[MAX_CHAVES];                // IDs
    struct NoBMais *filhos[ORDEM_M];             // apenas em nós internos
    PonteiroRegistro *ptrs[MAX_CHAVES];          // apenas em folhas
    struct NoBMais *next;                        // encadeamento entre folhas
} NoBMais;

typedef struct {
    NoBMais *raiz;
} ArvoreBMais;

typedef struct {
    long long chave_promovida; // primeira chave do irmão direito (folha) ou mediana (interno)
    NoBMais *novo_irmao;       // nó à direita após divisão
} SplitResultado;
//cria arvore para funcoes
ArvoreBMais indiceProdutos;

NoBMais *criarNo(bool eh_folha) {
    NoBMais *n = (NoBMais*) malloc(sizeof(NoBMais));
    n->eh_folha = eh_folha;
    n->num_chaves = 0;
    n->next = NULL;
    if (!eh_folha) {
        for (int i = 0; i < ORDEM_M; i++) n->filhos[i] = NULL;
    }
    for (int i = 0; i < MAX_CHAVES; i++) n->ptrs[i] = NULL;
    return n;
}

SplitResultado *dividirNo(NoBMais *no_cheio,
                          long long id_a_inserir,
                          PonteiroRegistro *ptr_registro,
                          NoBMais *filho_a_inserir,
                          int pos_insercao);

SplitResultado *inserirRecursivo(ArvoreBMais *arvore,
                                 NoBMais *no_atual,
                                 long long id,
                                 PonteiroRegistro *ptr_registro,
                                 NoBMais *filho_promovido) {
    int i = 0;
    while (i < no_atual->num_chaves && id > no_atual->chaves[i]) i++;

    if (!no_atual->eh_folha) {
        SplitResultado *rs = inserirRecursivo(arvore, no_atual->filhos[i], id, ptr_registro, NULL);
        if (rs == NULL) return NULL;

        // Inserir chave promovida e ponteiro para novo irmão no nó interno atual
        long long k = rs->chave_promovida;
        NoBMais *novo = rs->novo_irmao;
        free(rs);

        // Encontrar posição para inserir k
        int j = 0;
        while (j < no_atual->num_chaves && k > no_atual->chaves[j]) j++;

        // Deslocar chaves e filhos para abrir espaço em no vetor
        for (int t = no_atual->num_chaves; t > j; t--) {
            no_atual->chaves[t] = no_atual->chaves[t - 1];
        }
        for (int t = no_atual->num_chaves + 1; t > j + 1; t--) {
            no_atual->filhos[t] = no_atual->filhos[t - 1];
        }

        no_atual->chaves[j] = k;
        no_atual->filhos[j + 1] = novo;
        no_atual->num_chaves++;

        if (no_atual->num_chaves <= MAX_CHAVES) {
            return NULL;
        } else {
            // dividir nó interno cheio (sem ptr_registro)
            return dividirNo(no_atual, -1, NULL, NULL, -1);
        }
    }

    // Nó folha
    // Se id já existe, atualiza o offset (política de duplicata)
    if (i < no_atual->num_chaves && no_atual->chaves[i] == id) {
        no_atual->ptrs[i]->offset = ptr_registro->offset;
        return NULL;
    }

    // Espaço disponível: inserir na folha
    if (no_atual->num_chaves < MAX_CHAVES) {
        for (int j = no_atual->num_chaves; j > i; j--) {
            no_atual->chaves[j] = no_atual->chaves[j - 1];
            no_atual->ptrs[j] = no_atual->ptrs[j - 1];
        }
        no_atual->chaves[i] = id;
        no_atual->ptrs[i] = ptr_registro;
        no_atual->num_chaves++;
        return NULL;
    }

    // Folha cheia: dividir
    return dividirNo(no_atual, id, ptr_registro, NULL, i);
}

void inserirNaArvore(ArvoreBMais *arvore, long long id, long long offset) {
    if (arvore == NULL) return;

    if (arvore->raiz == NULL) {
        arvore->raiz = criarNo(true); // primeira raiz é folha
    }

    PonteiroRegistro *ptr_registro = (PonteiroRegistro*) malloc(sizeof(PonteiroRegistro));
    ptr_registro->id = id;
    ptr_registro->offset = offset;

    SplitResultado *rs = inserirRecursivo(arvore, arvore->raiz, id, ptr_registro, NULL);
    if (rs != NULL) {
        // criar nova raiz interna
        NoBMais *nova = criarNo(false);
        nova->chaves[0] = rs->chave_promovida;
        nova->filhos[0] = arvore->raiz;
        nova->filhos[1] = rs->novo_irmao;
        nova->num_chaves = 1;
        arvore->raiz = nova;
        free(rs);
    }
}

SplitResultado *dividirNo(NoBMais *no_cheio,
                          long long id_a_inserir,
                          PonteiroRegistro *ptr_registro,
                          NoBMais *filho_a_inserir,
                          int pos_insercao) {
    SplitResultado *out = (SplitResultado*) malloc(sizeof(SplitResultado));
    NoBMais *irmao = criarNo(no_cheio->eh_folha);

    if (no_cheio->eh_folha) {
        // criar arrays temporários para chaves+ptrs (tamanho MAX_CHAVES+1)
        long long tmp_keys[MAX_CHAVES + 1];
        PonteiroRegistro *tmp_ptrs[MAX_CHAVES + 1];

        // copiar existentes
        for (int i = 0; i < no_cheio->num_chaves; i++) {
            tmp_keys[i] = no_cheio->chaves[i];
            tmp_ptrs[i] = no_cheio->ptrs[i];
        }

        // inserir novo par na posição pos_insercao
        for (int j = no_cheio->num_chaves; j > pos_insercao; j--) {
            tmp_keys[j] = tmp_keys[j - 1];
            tmp_ptrs[j] = tmp_ptrs[j - 1];
        }
        tmp_keys[pos_insercao] = id_a_inserir;
        tmp_ptrs[pos_insercao] = ptr_registro;

        int total = no_cheio->num_chaves + 1;
        int split = total / 2; // política simples (pode ajustar para B+)

        // esquerda fica no_cheio
        no_cheio->num_chaves = split;
        for (int i = 0; i < split; i++) {
            no_cheio->chaves[i] = tmp_keys[i];
            no_cheio->ptrs[i] = tmp_ptrs[i];
        }

        // direita vai para irmao
        int right_count = total - split;
        irmao->num_chaves = right_count;
        for (int i = 0; i < right_count; i++) {
            irmao->chaves[i] = tmp_keys[split + i];
            irmao->ptrs[i] = tmp_ptrs[split + i];
        }

        // encadeamento de folhas
        irmao->next = no_cheio->next;
        no_cheio->next = irmao;

        // chave promovida: primeira chave do irmão direito
        out->chave_promovida = irmao->chaves[0];
        out->novo_irmao = irmao;
        return out;
    } else {
        // Nó interno: dividir chaves e filhos
        long long tmp_keys[MAX_CHAVES + 1];
        NoBMais *tmp_child[ORDEM_M + 1];

        for (int i = 0; i < no_cheio->num_chaves; i++) tmp_keys[i] = no_cheio->chaves[i];
        for (int i = 0; i <= no_cheio->num_chaves; i++) tmp_child[i] = no_cheio->filhos[i];

        // inserir chave promovida (de nível inferior) em posição correta
        int i = 0;
        while (i < no_cheio->num_chaves && id_a_inserir > tmp_keys[i]) i++;

        for (int t = no_cheio->num_chaves; t > i; t--) tmp_keys[t] = tmp_keys[t - 1];
        for (int t = no_cheio->num_chaves + 1; t > i + 1; t--) tmp_child[t] = tmp_child[t - 1];

        tmp_keys[i] = id_a_inserir;
        tmp_child[i + 1] = filho_a_inserir;

        int total = no_cheio->num_chaves + 1;
        int mid = total / 2; // mediana promovida

        // esquerda (no_cheio) recebe [0..mid-1]
        no_cheio->num_chaves = mid;
        for (int k = 0; k < mid; k++) no_cheio->chaves[k] = tmp_keys[k];
        for (int k = 0; k <= mid; k++) no_cheio->filhos[k] = tmp_child[k];

        // promovida
        long long promovida = tmp_keys[mid];

        // direita (irmao) recebe [mid+1..total-1]
        int right_keys = total - mid - 1;
        irmao->num_chaves = right_keys;
        for (int k = 0; k < right_keys; k++) irmao->chaves[k] = tmp_keys[mid + 1 + k];
        for (int k = 0; k <= right_keys; k++) irmao->filhos[k] = tmp_child[mid + 1 + k];

        out->chave_promovida = promovida;
        out->novo_irmao = irmao;
        return out;
    }
}

long long buscarOffset(ArvoreBMais *arvore, long long id) {
    NoBMais *n = arvore->raiz;
    if (!n) return -1; // Árvore vazia

    // Navega até a folha
    while (!n->eh_folha) {
        int i = 0;
        while (i < n->num_chaves && id > n->chaves[i]) i++;
        
        // --- CORREÇÃO DE SEGURANÇA AQUI ---
        // Se o filho for NULL (erro estrutural ou fim de caminho inesperado),
        // retornamos -1 para evitar o travamento (crash) do programa.
        if (n->filhos[i] == NULL) {
            return -1; 
        }
        // ----------------------------------

        n = n->filhos[i];
    }

    // Busca sequencial dentro da folha encontrada
    for (int i = 0; i < n->num_chaves; i++) {
        if (n->chaves[i] == id) {
            // Mais uma segurança: verifica se o ponteiro de registro existe
            if (n->ptrs[i] != NULL) {
                return n->ptrs[i]->offset;
            } else {
                return -1;
            }
        }
    }
    return -1;
}

void recarregarArvore(ArvoreBMais *arvore) {
    // 1. Limpar a árvore atual (Idealmente faria um free recursivo, 
    // mas para simplificar, vamos apenas resetar a raiz se for aceitável vazar memória 
    // em projeto acadêmico, ou você implementa uma função liberarArvore)
    arvore->raiz = NULL; 

    // 2. Ler arquivo e reinserir tudo
    FILE *arq = fopen(PRODFILE, "rb");
    if (arq != NULL) {
        Produto prod;
        long long offset = 0;
        // Reinicializa a árvore do zero
        while (fread(&prod, sizeof(Produto), 1, arq) == 1) {
            inserirNaArvore(arvore, prod.id, offset);
            offset += sizeof(Produto);
        }
        fclose(arq);
    }
}

long long pesquisa(long long id){
    //retorna o offset do registro, assume que o arquivo já esteja ordenado


    FILE *bin = fopen(PRODFILE,"rb");
    fseek(bin,0,SEEK_END);
    int filesize = ftell(bin);
    int numRegistros = filesize/sizeof(Produto);
    int low = 0;
    int high = numRegistros-1;

    int mid; //começar no registro do meio do arquivo
    Produto prodAtual;
    int cont=0;

    while(low <= high){
        cont++;
        mid = low + (high-low)/2; //estou usando uma logica de delimitar a regiao do "vetor" 
                                //com 2 auxiliares para controlar a regiao do arquivo sendo observada
        
        fseek(bin,mid*sizeof(Produto),SEEK_SET);
        fread(&prodAtual,sizeof(Produto),1,bin);

        if(prodAtual.id == id){
            printf("Registro encontrado, %d comparacoes necessarias\n",cont);
            printf("%lld - %s - %.2f", prodAtual.id, prodAtual.categoria, prodAtual.preco);
            fclose(bin);
            return mid*sizeof(Produto);
        }
        else if(id < prodAtual.id)
            high = mid-1;
        else
            low = mid+1;
    }

    printf("Registro nao encontrado, %d comparacoes feitas",cont);
    fclose(bin);

    return -1; //nao encontrado

}

long long pesquisa_pedido(long long id){
    //retorna o offset do registro, assume que o arquivo já esteja ordenado


    FILE *bin = fopen(ORDERFILE,"rb");
    fseek(bin,0,SEEK_END);
    int filesize = ftell(bin);
    int numRegistros = filesize/sizeof(Pedido);
    int low = 0;
    int high = numRegistros-1;

    int mid; //começar no registro do meio do arquivo
    Pedido atual;
    int cont=0;

    while(low <= high){
        cont++;
        mid = low + (high-low)/2; //estou usando uma logica de delimitar a regiao do "vetor" 
                                //com 2 auxiliares para controlar a regiao do arquivo sendo observada
        
        fseek(bin,mid*sizeof(Pedido),SEEK_SET);
        fread(&atual,sizeof(Pedido),1,bin);

        if(atual.id == id){
            printf("Registro encontrado, %d comparacoes necessarias\n",cont);
            printf("%lld - %lld - %s", atual.id, atual.id_produto, atual.data);
            fclose(bin);
            return mid*sizeof(Pedido);
        }
        else if(id < atual.id)
            high = mid-1;
        else
            low = mid+1;
    }

    printf("Registro nao encontrado, %d comparacoes feitas",cont);
    fclose(bin);

    return -1; //nao encontrado

}

void construirIndiceProdutos() {
    FILE *arq_dados = fopen(PRODFILE, "rb");
    FILE *arq_indice = fopen(PROD_IDX_FILE, "wb");

    if (arq_dados == NULL || arq_indice == NULL) {
        printf("Erro ao abrir arquivos para construcao do indice de produtos.\n");
        if (arq_dados) fclose(arq_dados);
        if (arq_indice) fclose(arq_indice);
        return;
    }

    Produto prodAtual;
    Indice idx;
    long long contador = 0;
    long long current_offset = 0;
    
    // Varre o arquivo de dados
    while (fread(&prodAtual, sizeof(Produto), 1, arq_dados) == 1) {
        
        // Se o contador for um multiplo do FATOR_INDICE, cria um registro de índice
        if (contador % FATOR_INDICE == 0) {
            idx.id = prodAtual.id;
            idx.offset = current_offset;
            
            // Escreve o registro de índice no arquivo .idx
            fwrite(&idx, sizeof(Indice), 1, arq_indice);
        }

        // Atualiza o offset e o contador
        current_offset += sizeof(Produto);
        contador++;
    }

    printf("Indice parcial de Produtos construido com %lld entradas (Fator %d).\n", 
           contador / FATOR_INDICE + (contador % FATOR_INDICE != 0 ? 1 : 0), FATOR_INDICE);

    fclose(arq_dados);
    fclose(arq_indice);
}

long long pesquisa_com_indice(long long id_buscado) {
    FILE *arq_indice = fopen(PROD_IDX_FILE, "rb");
    if (arq_indice == NULL) {
        printf("Erro: Arquivo de indice (%s) nao encontrado. Tentando pesquisa binaria normal...\n", PROD_IDX_FILE);
        // Se o índice não existir, recorre à busca binária original no arquivo de dados.
        return pesquisa(id_buscado); 
    }

    // FASE 1: Pesquisa Binária no Arquivo de Índice
    fseek(arq_indice, 0, SEEK_END);
    long idx_filesize = ftell(arq_indice);
    int numRegistrosIdx = idx_filesize / sizeof(Indice);
    int low_idx = 0;
    int high_idx = numRegistrosIdx - 1;
    long long inicio_bloco_offset = 0; // Onde comecar a busca no arquivo .bin
    int cont_idx = 0;

    // Busca binária para encontrar o ponto de partida
    while (low_idx <= high_idx) {
        cont_idx++;
        int mid_idx = low_idx + (high_idx - low_idx) / 2;
        
        fseek(arq_indice, mid_idx * sizeof(Indice), SEEK_SET);
        Indice idxAtual;
        fread(&idxAtual, sizeof(Indice), 1, arq_indice);

        if (id_buscado >= idxAtual.id) {
            inicio_bloco_offset = idxAtual.offset; // Este é o melhor ponto de partida até agora
            low_idx = mid_idx + 1; // Tenta encontrar um ponto de partida ainda maior
        } else {
            high_idx = mid_idx - 1;
        }
    }
    
    printf("Pesquisa no indice: %d comparacoes.\n", cont_idx);
    fclose(arq_indice);

    // FASE 2: Busca Sequencial Limitada no Arquivo de Dados
    FILE *arq_dados = fopen(PRODFILE, "rb");
    if (arq_dados == NULL) return -1;

    // Posiciona o ponteiro no offset encontrado no índice
    fseek(arq_dados, inicio_bloco_offset, SEEK_SET);

    Produto prodAtual;
    long long cont_seq = 0;
    long long max_comparacoes = FATOR_INDICE * 2; // Limite de busca para nao varrer o arquivo
    long long offset_encontrado = -1;

    // Busca sequencial no bloco
    while (fread(&prodAtual, sizeof(Produto), 1, arq_dados) == 1 && cont_seq < max_comparacoes) {
        if (prodAtual.id == id_buscado) {
            // Calcula o offset atual para retorno
            offset_encontrado = ftell(arq_dados) - sizeof(Produto);
            printf("Registro encontrado! Busca sequencial: %lld comparacoes.\n", cont_seq + 1);
            printf("%lld - %s - %.2f\n", prodAtual.id, prodAtual.categoria, prodAtual.preco);
            break;
        } else if (prodAtual.id > id_buscado) {
             // Como o arquivo está ordenado, se o ID atual for maior, o registro não existe.
            break; 
        }
        cont_seq++;
    }

    fclose(arq_dados);
    
    if (offset_encontrado == -1) {
        printf("Registro nao encontrado no bloco.\n");
    }
    return offset_encontrado; 
}

void construirIndicePedidos() {
    FILE *arq_dados = fopen(ORDERFILE, "rb");
    FILE *arq_indice = fopen(ORDER_IDX_FILE, "wb");

    if (arq_dados == NULL) {
        // Se o arquivo de dados não existe, não há índice a construir
        if (arq_indice) fclose(arq_indice);
        return;
    }
    if (arq_indice == NULL) {
        printf("Erro ao criar o arquivo de indice %s.\n", ORDER_IDX_FILE);
        fclose(arq_dados);
        return;
    }

    Pedido pedAtual;
    Indice idx;
    long long contador = 0;
    long long current_offset = 0;
    
    // Varre o arquivo de dados
    while (fread(&pedAtual, sizeof(Pedido), 1, arq_dados) == 1) {
        
        // Cria um registro de índice a cada FATOR_INDICE
        if (contador % FATOR_INDICE == 0) {
            idx.id = pedAtual.id;
            idx.offset = current_offset;
            
            // Escreve o registro de índice no arquivo .idx
            fwrite(&idx, sizeof(Indice), 1, arq_indice);
        }

        // Atualiza o offset e o contador
        current_offset += sizeof(Pedido);
        contador++;
    }

    printf("Indice parcial de Pedidos construido/reconstruido.\n");

    fclose(arq_dados);
    fclose(arq_indice);
}

long long pesquisa_com_indice_pedido(long long id_buscado) {
    FILE *arq_indice = fopen(ORDER_IDX_FILE, "rb");
    if (arq_indice == NULL) {
        printf("Arquivo de indice (%s) nao encontrado. Tentando pesquisa binaria normal...\n", ORDER_IDX_FILE);
        // Recorre à busca binária original (pesquisa_pedido)
        return pesquisa_pedido(id_buscado); 
    }

    // FASE 1: Pesquisa Binária no Arquivo de Índice (.idx)
    fseek(arq_indice, 0, SEEK_END);
    long idx_filesize = ftell(arq_indice);
    int numRegistrosIdx = idx_filesize / sizeof(Indice);
    int low_idx = 0;
    int high_idx = numRegistrosIdx - 1;
    long long inicio_bloco_offset = 0; // Onde começar a busca no arquivo .bin
    int cont_idx = 0;

    while (low_idx <= high_idx) {
        cont_idx++;
        int mid_idx = low_idx + (high_idx - low_idx) / 2;
        
        fseek(arq_indice, mid_idx * sizeof(Indice), SEEK_SET);
        Indice idxAtual;
        fread(&idxAtual, sizeof(Indice), 1, arq_indice);

        if (id_buscado >= idxAtual.id) {
            inicio_bloco_offset = idxAtual.offset; 
            low_idx = mid_idx + 1; 
        } else {
            high_idx = mid_idx - 1;
        }
    }
    
    printf("Pesquisa no indice de pedidos: %d comparacoes.\n", cont_idx);
    fclose(arq_indice);

    // FASE 2: Busca Sequencial Limitada no Arquivo de Dados (.bin)
    FILE *arq_dados = fopen(ORDERFILE, "rb");
    if (arq_dados == NULL) return -1;

    // Posiciona o ponteiro no offset encontrado no índice
    fseek(arq_dados, inicio_bloco_offset, SEEK_SET);

    Pedido pedAtual;
    long long cont_seq = 0;
    long long max_comparacoes = FATOR_INDICE * 2; 
    long long offset_encontrado = -1;

    while (fread(&pedAtual, sizeof(Pedido), 1, arq_dados) == 1 && cont_seq < max_comparacoes) {
        if (pedAtual.id == id_buscado) {
            offset_encontrado = ftell(arq_dados) - sizeof(Pedido);
            printf("Registro de pedido encontrado! Busca sequencial: %lld comparacoes.\n", cont_seq + 1);
            printf("%lld - %lld - %s\n", pedAtual.id, pedAtual.id_produto, pedAtual.data);
            break;
        } else if (pedAtual.id > id_buscado) {
            // Arquivo ordenado: se ultrapassou o ID, ele não existe
            break; 
        }
        cont_seq++;
    }

    fclose(arq_dados);
    
    if (offset_encontrado == -1) {
        printf("Registro de pedido nao encontrado no bloco.\n");
    }
    return offset_encontrado; 
}


// Funcao auxiliar para aplicar espacos em branco para garantir tamnho fixo em todos registros 
void aplicar_padding(char *string, size_t max_size) {
    size_t len = strlen(string);
    // Se a string for maior que o maximo permitido, trunca 
    if (len >= max_size) {
        string[max_size - 1] = '\0';
        return;
    }

    // Preenche o restante dos caracteres com espaco
    for (size_t i = len; i < max_size - 1; i++) {
        string[i] = ' ';
    }
    string[max_size - 1] = '\0'; // Garante o nulo na ultima posicao 
}


long long encontrar_ponto_insercao(long long id) { //fun aux para inserir
    FILE *bin = fopen(PRODFILE, "rb");
    if (bin == NULL) {
        return 0; // Se o arquivo nao existe, o ponto de insercao e o inicio (offset 0)
    }

    fseek(bin, 0, SEEK_END);
    long filesize = ftell(bin);
    int numRegistros = filesize / sizeof(Produto);
    int low = 0;
    int high = numRegistros - 1;

    Produto prodAtual;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        
        // Leitura do registro no meio
        fseek(bin, mid * sizeof(Produto), SEEK_SET);
        fread(&prodAtual, sizeof(Produto), 1, bin);

        if (id == prodAtual.id) {
            // Este caso nao deve ocorrer se o ID for gerado sequencialmente.
            // Se ocorrer, indica duplicidade, e a insercao nao deve prosseguir.
            fclose(bin);
            return -1; // Sinaliza que o ID ja existe
        } else if (id < prodAtual.id) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    
    // Low é o índice do primeiro registro maior que o ID buscado.
    // O ponto de insercao (offset) é: low * sizeof(Produto)
    fclose(bin);
    return low * sizeof(Produto);
}

void inserirProduto(Produto produto) {

    FILE *arqProdutos = fopen(PRODFILE, "rb+");
    long long ultimo_id = 0;
    long filesize = 0;
    
    if (arqProdutos == NULL) {
        // Se nao existe, cria e o ID é 1.
        arqProdutos = fopen(PRODFILE, "wb+");
        if (arqProdutos == NULL) {
            printf("Erro ao criar ou abrir o arquivo %s\n", PRODFILE);
            return;
        }
        produto.id = 1;
        fwrite(&produto, sizeof(Produto), 1, arqProdutos);
        printf("Arquivo criado e Produto inserido com sucesso.\n");
        fclose(arqProdutos);
        return;
    }
    
    // Se existe, determina o ID para o novo produto
    fseek(arqProdutos, 0, SEEK_END);
    filesize = ftell(arqProdutos);

    if (filesize > 0) {
        // Le o ID do ultimo para gerar o novo ID
        fseek(arqProdutos, -(long)sizeof(Produto), SEEK_END);
        Produto ultimo;
        fread(&ultimo, sizeof(Produto), 1, arqProdutos);
        ultimo_id = ultimo.id;
    }
    produto.id = ultimo_id + 1;

    // Se o arquivo não estava vazio, encontra onde inserir.
    long long offset_insercao = encontrar_ponto_insercao(produto.id);

    // Se a busca falhar (ID já existe, retorna -1), não insere.
    if (offset_insercao == -1) {
        printf("Erro: ID %lld ja existe.\n", produto.id);
        fclose(arqProdutos);
        return;
    }

    //Desloca Registros

    long bytes_a_mover = filesize - offset_insercao;
    
    //Aumenta o arquivo em 1 registro (coloca o ponteiro no novo final)
    fseek(arqProdutos, 0, SEEK_END);
    fwrite(&produto, sizeof(Produto), 1, arqProdutos); // Escreve lixo no final para aumentar o tamanho do arquivo.
    
    //Move o bloco de dados
    if (bytes_a_mover > 0) {
        
        // Loop para mover os registros, um por um, de tras para frente
        long long current_offset = filesize - sizeof(Produto); 
        long long destination_offset = filesize;               

        Produto temp; // Buffer de 1 registro

        // Move os registros do fim do arquivo ate local de insercao
        while (current_offset >= offset_insercao) {
            
            // Le o registro
            fseek(arqProdutos, current_offset, SEEK_SET);
            fread(&temp, sizeof(Produto), 1, arqProdutos);
            
            // Escreve o registro na proxima posicao 
            fseek(arqProdutos, destination_offset, SEEK_SET);
            fwrite(&temp, sizeof(Produto), 1, arqProdutos);
            
            // Atualiza os offsets
            current_offset -= sizeof(Produto);
            destination_offset -= sizeof(Produto);
        }
    }
    
    //Insere o Novo Registro
    
    fseek(arqProdutos, offset_insercao, SEEK_SET);
    fwrite(&produto, sizeof(Produto), 1, arqProdutos);

    printf("Produto inserido na posicao %lld com sucesso.\n", offset_insercao / sizeof(Produto));
    fclose(arqProdutos);

    construirIndiceProdutos(); 
    inserirNaArvore(&indiceProdutos, produto.id, offset_insercao);
}


long long encontrar_ponto_insercao_pedido(long long id) {
    FILE *bin = fopen(ORDERFILE, "rb");
    if (bin == NULL) {
        return 0; // Se o arquivo nao existe, o ponto de insercao e o inicio (offset 0)
    }

    fseek(bin, 0, SEEK_END);
    long filesize = ftell(bin);
    int numRegistros = filesize / sizeof(Pedido);
    int low = 0;
    int high = numRegistros - 1;

    Pedido pedAtual;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        
        // Leitura do registro no meio
        fseek(bin, mid * sizeof(Pedido), SEEK_SET);
        fread(&pedAtual, sizeof(Pedido), 1, bin);

        if (id == pedAtual.id) {
            // ID já existe
            fclose(bin);
            return -1; 
        } else if (id < pedAtual.id) {
            high = mid - 1;
        } else {
            low = mid + 1;
        }
    }
    
    // Low é o índice do primeiro registro maior que o ID buscado.
    // O ponto de insercao (offset) é: low * sizeof(Pedido)
    fclose(bin);
    return low * sizeof(Pedido);
}

void inserirPedido(Pedido pedido) {

    FILE *arqPedidos = fopen(ORDERFILE, "rb+");
    long long ultimo_id = 0;
    long filesize = 0;
    
    if (arqPedidos == NULL) {
        // Se nao existe, cria e ID é 1.
        arqPedidos = fopen(ORDERFILE, "wb+");
        if (arqPedidos == NULL) {
            printf("Erro ao criar ou abrir o arquivo %s\n", ORDERFILE);
            return;
        }
        pedido.id = 1;
        fwrite(&pedido, sizeof(Pedido), 1, arqPedidos);
        printf("Arquivo criado e Pedido inserido com sucesso.\n");
        fclose(arqPedidos);
        return;
    }
    
    // Se existe, determina o ID para o novo pedido
    fseek(arqPedidos, 0, SEEK_END);
    filesize = ftell(arqPedidos);

    if (filesize > 0) {
        // Le o ID do ultimo para gerar o novo ID
        fseek(arqPedidos, -(long)sizeof(Pedido), SEEK_END); // Usando (long) para evitar o warning
        Pedido ultimo;
        fread(&ultimo, sizeof(Pedido), 1, arqPedidos);
        ultimo_id = ultimo.id;
    }
    pedido.id = ultimo_id + 1;

    // Se o arquivo não estava vazio, vamos encontrar onde inserir.
    long long offset_insercao = encontrar_ponto_insercao_pedido(pedido.id);

    // Se a busca falhar, não insere.
    if (offset_insercao == -1) {
        printf("Erro: ID %lld ja existe.\n", pedido.id);
        fclose(arqPedidos);
        return;
    }

    // Desloca Registros para Abrir Espaço 

    long bytes_a_mover = filesize - offset_insercao;
    
    // Aumenta o arquivo em 1 registro (coloca o ponteiro no novo final)
    fseek(arqPedidos, 0, SEEK_END);
    fwrite(&pedido, sizeof(Pedido), 1, arqPedidos); // Escreve lixo no final para aumentar o tamanho do arquivo.
    
    // Move o bloco de dados
    if (bytes_a_mover > 0) {
        
        long long current_offset = filesize - sizeof(Pedido); 
        long long destination_offset = filesize;               

        Pedido temp; // Buffer de 1 registro

        // Move os registros do fim do arquivo ate o ponto de insercao
        while (current_offset >= offset_insercao) {
            
            // Le o registro
            fseek(arqPedidos, current_offset, SEEK_SET);
            fread(&temp, sizeof(Pedido), 1, arqPedidos);
            
            // Escreve o registro na proxima posicao (deslocando-o)
            fseek(arqPedidos, destination_offset, SEEK_SET);
            fwrite(&temp, sizeof(Pedido), 1, arqPedidos);
            
            // Atualiza os offsets
            current_offset -= sizeof(Pedido);
            destination_offset -= sizeof(Pedido);
        }
    }
    
    // Insere o Novo Registro 
    
    fseek(arqPedidos, offset_insercao, SEEK_SET);
    fwrite(&pedido, sizeof(Pedido), 1, arqPedidos);

    printf("Pedido inserido na posicao %lld com sucesso.\n", offset_insercao / sizeof(Pedido));
    fclose(arqPedidos);

    construirIndicePedidos();
}


//Remoção por blocos para evitar colocar tudo em RAM
void removerProduto(Produto produto){
    // 1) Descobre onde está o registro no arquivo (offset em bytes)
    long long offset = pesquisa(produto.id);
    if (offset == -1) return; // não achou: a própria pesquisa já imprime msg

    // 2) Abre o arquivo original para leitura binária
    FILE *orig = fopen(PRODFILE, "rb");
    if (!orig) { printf("Erro abrindo %s\n", PRODFILE); return; }

    // 3) Cria um arquivo temporário que será a "cópia sem o registro"
    FILE *temp = fopen("temp.bin", "wb");
    if (!temp) { printf("Erro criando temp.bin\n"); fclose(orig); return; }

    // 4) Mede o tamanho total do arquivo para saber até onde copiar
    fseek(orig, 0, SEEK_END);
    long long filesize = (long long) ftell(orig);

    // Quantidade de bytes ANTES do registro a excluir (copiaremos para o temp)
    long long bytes_before = offset;
    // Quantidade de bytes DEPOIS do registro (não precisamos calcular, copiamos até EOF)
    long long bytes_after  = filesize - (offset + (long long)sizeof(Produto)); // (informativo)

    // 5) Volta para o início para começar a copiar a parte "antes"
    fseek(orig, 0, SEEK_SET);

    // 6) Buffer fixo e pequeno: evita alocar o arquivo todo na RAM
    char buf[64 * 1024];
    size_t n;

    // 7) Copia os bytes do início até o início do registro (região [0, offset))
    while (bytes_before > 0) {
        // lê em blocos de até 64 KB (ou o que faltar)
        size_t chunk = (size_t)(bytes_before > (long long)sizeof(buf) ? sizeof(buf) : bytes_before);
        n = fread(buf, 1, chunk, orig);
        if (n == 0) break;           // EOF ou erro de leitura
        fwrite(buf, 1, n, temp);
        bytes_before -= (long long)n;
    }

    // 8) "Pula" o registro a ser removido (avança o ponteiro do arquivo original)
    fseek(orig, offset + (long long)sizeof(Produto), SEEK_SET);

    // 9) Copia o restante do arquivo (do byte seguinte ao registro até o EOF)
    while ((n = fread(buf, 1, sizeof(buf), orig)) > 0) {
        fwrite(buf, 1, n, temp);
    }

    // 10) Fecha arquivos
    fclose(orig);
    fclose(temp);

    // 11) Substitui o original pelo temporário (remoção efetiva)
    remove(PRODFILE);
    rename("temp.bin", PRODFILE);

    printf("\nRegistro deletado com sucesso\n");

    // 12) Reconstrói o índice porque os offsets mudaram
    construirIndiceProdutos();
}


void removerPedido(Pedido pedido){
    // 1) Acha o offset do pedido pelo ID (arquivo está ordenado)
    long long offset = pesquisa_pedido(pedido.id);
    if (offset == -1) return; // não encontrado

    // 2) Abre o arquivo original (somente leitura)
    FILE *orig = fopen(ORDERFILE, "rb");
    if (!orig) { printf("Erro abrindo %s\n", ORDERFILE); return; }

    // 3) Cria o arquivo temporário (escrita)
    FILE *temp = fopen("temp.bin", "wb");
    if (!temp) { printf("Erro criando temp.bin\n"); fclose(orig); return; }

    // 4) Mede o tamanho total do arquivo
    fseek(orig, 0, SEEK_END);
    long long filesize = (long long) ftell(orig);

    // Bytes ANTES do registro a remover
    long long bytes_before = offset;
    // Bytes DEPOIS do registro (não precisamos usar explicitamente)
    long long bytes_after  = filesize - (offset + (long long)sizeof(Pedido)); // (informativo)

    // 5) Volta ao início para copiar a parte anterior
    fseek(orig, 0, SEEK_SET);

    // 6) Buffer fixo para copiar em blocos
    char buf[64 * 1024];
    size_t n;

    // 7) Copia [0 .. offset)
    while (bytes_before > 0) {
        size_t chunk = (size_t)(bytes_before > (long long)sizeof(buf) ? sizeof(buf) : bytes_before);
        n = fread(buf, 1, chunk, orig);
        if (n == 0) break;
        fwrite(buf, 1, n, temp);
        bytes_before -= (long long)n;
    }

    // 8) Pula o registro alvo
    fseek(orig, offset + (long long)sizeof(Pedido), SEEK_SET);

    // 9) Copia o restante até o fim
    while ((n = fread(buf, 1, sizeof(buf), orig)) > 0) {
        fwrite(buf, 1, n, temp);
    }

    // 10) Fecha arquivos
    fclose(orig);
    fclose(temp);

    // 11) Substitui o original pelo temporário
    remove(ORDERFILE);
    rename("temp.bin", ORDERFILE);

    printf("\nRegistro deletado com sucesso\n");

    // 12) Reconstrói o índice de pedidos
    construirIndicePedidos();
}


void limpar_buffer() {
    int c;
    // Consome caracteres até encontrar o caractere de nova linha ('\n')
    // ou o final do arquivo (EOF)
    while ((c = getchar()) != '\n' && c != EOF) {
    }
}

void mostrarProdutos() {
    FILE *arq = fopen(PRODFILE, "rb");

    if (arq == NULL) {
        printf("\nO arquivo de produtos (%s) nao existe ou esta vazio.\n", PRODFILE);
        return;
    }

    Produto prodAtual;
    int cont = 0;

    printf("\n--- LISTA DE PRODUTOS ---\n");
    printf("--------------------------------------------------\n");
    printf("| ID        | Categoria            | Preco      |\n");
    printf("--------------------------------------------------\n");

    // Loop de leitura: continua lendo enquanto fread retorna 1 (sucesso)
    while (fread(&prodAtual, sizeof(Produto), 1, arq) == 1) {
        
        printf("| %-9lld | %-20s | R$ %-7.2f |\n", 
               prodAtual.id, prodAtual.categoria, prodAtual.preco);
        cont++;
    }

    printf("--------------------------------------------------\n");
    printf("Total de %d registros encontrados.\n", cont);

    fclose(arq);
}

void mostrarPedidos() {
    FILE *arq = fopen(ORDERFILE, "rb");

    if (arq == NULL) {
        printf("\nO arquivo de pedidos (%s) nao existe ou esta vazio.\n", ORDERFILE);
        return;
    }

    Pedido pedAtual;
    int cont = 0;

    printf("\n--- LISTA DE PEDIDOS ---\n");
    printf("---------------------------------------\n");
    printf("| ID Pedido | ID Produto | Data       |\n");
    printf("---------------------------------------\n");

    // Loop de leitura
    while (fread(&pedAtual, sizeof(Pedido), 1, arq) == 1) {
        // Exibe os dados do pedido. O campo data[11] deve ser exibido como string.
        printf("| %-9lld | %-10lld | %-10s |\n", 
               pedAtual.id, pedAtual.id_produto, pedAtual.data);
        cont++;
    }

    printf("---------------------------------------\n");
    printf("Total de %d registros encontrados.\n", cont);

    fclose(arq);
}

int main(){
    indiceProdutos.raiz = NULL;
    int op;
    construirIndiceProdutos(); 
    construirIndicePedidos();
    FILE *arq = fopen(PRODFILE, "rb");
    if (arq != NULL) {
        Produto prod;
        long long offset = 0;
        while (fread(&prod, sizeof(Produto), 1, arq) == 1) {
            inserirNaArvore(&indiceProdutos, prod.id, offset);
            offset += sizeof(Produto);
        }
        fclose(arq);
    }
    do{
        printf("\n------MENU PRINCIPAL------\n");
        printf("1. Produtos\n");
        printf("2. Pedidos\n");
        printf("0. Sair\n");
        printf("opcao: ");
        scanf("%d",&op);


        if(op==1){
            Produto produto;
            memset(produto.categoria, 0, sizeof(produto.categoria));
            int op_prod;
            printf("\n------Produtos------\n");
            printf("1. Adicionar\n");
            printf("2. Mostrar Dados\n");
            printf("3. Excluir\n");
            printf("4. Pesquisa Binaria\n");
            printf("5. Pesquisa via Arvore B+\n");
            printf("opcao: ");
            scanf("%d",&op_prod);

            if(op_prod == 1){

                printf("\nCategoria do produto: ");
                limpar_buffer();
                fgets(produto.categoria,sizeof(produto.categoria),stdin);
                produto.categoria[strcspn(produto.categoria, "\n\r")] = '\0'; //remove o \n
                aplicar_padding(produto.categoria, sizeof(produto.categoria)); //fun de tam fixo para registros
                printf("\nValor do produto: ");
                scanf("%f",&produto.preco);

                inserirProduto(produto);
                printf("Atualizando Arvore B+ em memoria...\n");
                recarregarArvore(&indiceProdutos);
                
            }
            else if (op_prod == 2){ 
                mostrarProdutos();   
            }
            else if (op_prod == 3 || op_prod == 4) {
                printf("\nID do produto: ");
                scanf("%lld",&produto.id);

                if(op_prod == 3){
                    removerProduto(produto);
                    printf("Atualizando Arvore B+ em memoria...\n");
                    recarregarArvore(&indiceProdutos);
                }
                else if(op_prod == 4)
                    pesquisa_com_indice(produto.id); 
            }
            else if (op_prod == 5) {
                printf("\nID do produto: ");
                scanf("%lld",&produto.id);

                long long offset = buscarOffset(&indiceProdutos, produto.id);
                if (offset != -1) {
                    FILE *arq = fopen(PRODFILE, "rb");
                    fseek(arq, offset, SEEK_SET);
                    Produto prod;
                    fread(&prod, sizeof(Produto), 1, arq);
                    printf("%lld - %s - %.2f\n", prod.id, prod.categoria, prod.preco);
                    fclose(arq);
                } else {
                    printf("Produto nao encontrado.\n");
                }
            }
        }
        
       
        else if (op == 2) { 
            Pedido pedido;
            // Inicializa a estrutura Pedido.
            memset(pedido.data, 0, sizeof(pedido.data)); 
            int op_ped;

            printf("\n------Pedidos------\n");
            printf("1. Adicionar\n");
            printf("2. Excluir\n");
            printf("3. Pesquisa Binaria\n");
            printf("4. Mostrar dados\n");
            printf("opcao: ");
            scanf("%d", &op_ped);

            if (op_ped == 1) {
                // Lógica para Inserir (requer id_produto e data)
                printf("\nID do produto associado: ");
                scanf("%lld", &pedido.id_produto);
                
                printf("\nData do pedido (DD/MM/AAAA): ");
                limpar_buffer();
                fgets(pedido.data, sizeof(pedido.data), stdin);
                pedido.data[strcspn(pedido.data, "\n\r")] = '\0'; // remove o \n
                
                aplicar_padding(pedido.data, sizeof(pedido.data)); 

                inserirPedido(pedido);
            } 
            else if (op_ped == 4) {
                // Lógica para Mostrar Dados (não requer ID)
                mostrarPedidos();
            }
            else if (op_ped == 2 || op_ped == 3) {
                // APENAS opções 2 (Excluir) e 3 (Pesquisa) pedem o ID do Pedido
                printf("\nID do pedido: ");
                scanf("%lld", &pedido.id);

                if (op_ped == 2)
                    removerPedido(pedido);
                else if(op_ped == 3)
                    // Usa a função de pesquisa otimizada com índice (Pesquisa Binária no índice + seek)
                    pesquisa_com_indice_pedido(pedido.id);
            }
        }

    } while (op != 0);
}