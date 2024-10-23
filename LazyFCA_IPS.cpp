#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <charconv>
#include <stdint.h>
#include <random>
#include <functional>
#include <thread>

struct DataClassRowBase {
    double Vx[28];
    double amount;
};

struct DataClassRow : DataClassRowBase {
    DataClassRow(const double Vx[28], const double &amount, bool is_fraud) {
        memcpy(this->Vx, Vx, sizeof(double[28]));
        this->amount = amount;
        this->is_fraud = is_fraud;
    }

    bool is_fraud;
};

struct DataRow {
    DataRow(const double Vx[28], const double &amount, bool is_fraud) {
        memcpy(this->Vx, Vx, sizeof(double[28]));
        this->amount = amount;
        this->is_fraud = is_fraud;
    }

    double Vx[28]; 
    double amount;
    bool is_fraud;
};

static inline void ReadDatasetSplitted(std::istream &csv_data, std::vector<DataRow> &train_data, std::vector<DataClassRow> &test_data, std::function<bool()> do_train) {
    double Vx[28];
    double amount;
    uint16_t is_fraud;

    std::string line;
    std::vector<std::string::size_type> positions;
    positions.reserve(31);
    std::string::size_type pos;

    std::getline(csv_data, line); // Skipping the header
    while (std::getline(csv_data, line)) {
        positions.clear();
        pos = line.find(',', 0); // Skipping the first column

        do {
            positions.emplace_back(pos);
            pos = line.find(',', pos + 1);
        } while (pos != std::string::npos);
        positions.emplace_back(line.size());

        for (uint8_t i = 0; i != 28; ++i) {
            std::from_chars(&line[positions[i] + 1], &line[positions[i + 1]], Vx[i]);
        }
        std::from_chars(&line[positions[28] + 1], &line[positions[29]], amount);
        std::from_chars(&line[positions[29] + 1], &line[positions[30]], is_fraud);

        if (do_train()) {
            train_data.emplace_back(Vx, amount, is_fraud);
        } else {
            test_data.emplace_back(Vx, amount, is_fraud);
        }
    }
}

struct CrossSection {
    CrossSection(const double Vx_a[28], const double Vx_b[28], double amount_a, double amount_b, bool is_fraud) {
        for (uint8_t i = 0; i != 28; ++i) {
            VxA_min[i] = std::min(Vx_a[i], Vx_b[i]);
            VxA_max[i] = std::max(Vx_a[i], Vx_b[i]);
        }
        VxA_min[28] = std::min(amount_a, amount_b);
        VxA_max[28] = std::max(amount_a, amount_b);
        this->is_fraud = is_fraud;
        positive = 1; // Crossection with itself
        negative = 0;
    }

    double VxA_min[29], VxA_max[29];
    bool is_fraud;
    uint32_t positive, negative;
};

static void Classify_th(std::vector<CrossSection> &crossections, uint8_t idx, uint8_t inc_size) {
    bool full_enclose;
    for (uint32_t bcri = idx, crsz = crossections.size(); bcri < crsz; bcri += inc_size) {
        CrossSection &bcr = crossections[bcri];
        for (uint32_t bcrj = 0; bcrj != crsz; ++bcrj) {
            if (bcri != bcrj) {
               const CrossSection &enclosed = crossections[bcrj];

                full_enclose = true;
                for (uint8_t i = 0; i != 29; ++i) {
                    if (bcr.VxA_min[i] > enclosed.VxA_min[i] || bcr.VxA_max[i] < enclosed.VxA_max[i]) {
                        full_enclose = false;
                        break;
                    }
                }
                if (full_enclose) {
                    if (bcr.is_fraud == enclosed.is_fraud) {
                        ++bcr.positive;
                    } else {
                        ++bcr.negative;
                    }
                }
            }
        }
        if (!(bcri & 0b11111111)) {
            std::cout << bcri * 100.0F / crsz << "%\n";
        }
    }
}

static inline void Classify(std::vector<DataRow> &train, const DataClassRowBase &to_class, uint32_t &is_fraud, uint32_t &not_is_fraud, float pos_neg_ratio = 0.01F, uint8_t threads_count = 4) {
    std::vector<CrossSection> crossections;
    crossections.reserve(train.size());

    for (const auto &row : train) {
        crossections.emplace_back(row.Vx, to_class.Vx, row.amount, to_class.amount, row.is_fraud);
    }

    std::vector<std::thread> threads;
    // Creating threads
    for (int i = 0; i != threads_count; ++i) {
        threads.emplace_back(std::thread(Classify_th, std::ref(crossections), i, threads_count));
    }

    // Waiting for threads
    for (auto &th : threads) {
        th.join();
    }

    is_fraud = 0;
    not_is_fraud = 0;
    for (const auto &cross : crossections) {
        if (cross.negative < cross.positive * pos_neg_ratio) {
            if (cross.is_fraud) {
                ++is_fraud;
            } else {
                ++not_is_fraud;
            }
        }
    }
}


int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    std::cout << "Enter the dataset filename [creditcard_2023.csv]: ";
    std::string input;
    std::getline(std::cin, input);
    if (input.empty()) {
        input = "creditcard_2023.csv";
    }

    std::cout << "Enter the CPUs count [4]: ";
    uint16_t cpus;
    std::cin >> cpus;
    if (!cpus) cpus = 1;

    std::cout << "Decrease classifier set by N times, 0 - no decrease [0]: ";
    uint16_t dectimes;
    std::cin >> dectimes;
    if (dectimes == 1)
        dectimes = 0;

    std::cout << "Enter the test items limit, 0 - no limit [0]: ";
    uint16_t tlimit;
    std::cin >> tlimit;

    std::cout << "Enter the test items offset, 0 - no offset [0]: ";
    uint16_t toffset;
    std::cin >> toffset;

    std::ifstream data_csv(input, std::ios::in);
    if (data_csv.fail()) {
        std::cout << "Error during opening the file: \"" << input << '"' << std::endl;
        return 0;
    }

    std::mt19937 gen(42);
    std::uniform_int_distribution<uint16_t> distribution(0, 2048);

    std::vector<DataRow> train;
    std::vector<DataClassRow> test;
    ReadDatasetSplitted(data_csv, train, test, [&]()->bool { return distribution(gen); });
    data_csv.close();

    if (dectimes) {
        std::vector<DataRow> train_decreased;
        train_decreased.reserve(train.size() / dectimes + 1);
        for (uint32_t i = 0, tsz = train.size(); i < tsz; i += dectimes) {
            train_decreased.emplace_back(train[i]);
        }
        train = std::move(train_decreased);
    } else {
        train.shrink_to_fit();
    }
    test.shrink_to_fit();

    if (tlimit) {
        std::vector<DataClassRow> tlimited;
        tlimited.reserve(tlimit);
        uint16_t pos_remains = tlimit / 2;
        uint16_t neg_remains = tlimit - pos_remains;

        for (const auto &item : test) {
            if (item.is_fraud) {
                if (pos_remains) {
                    --pos_remains;
                    tlimited.emplace_back(item);
                }
            } else {
                if (neg_remains) {
                    --neg_remains;
                    tlimited.emplace_back(item);
                }
            }
        }
        test = std::move(tlimited);
    }

    uint32_t is_fraud, not_is_fraud;

    uint32_t pos_c = 0, neg_c = 0;
    for (const auto &itm : train) {
        if (itm.is_fraud) {
            ++pos_c;
        } else {
            ++neg_c;
        }
    }

    std::ofstream resfile("results.txt", std::ios::out);
    resfile << "is_fraud records: " << pos_c << "\nnot_is_fraud records: " << neg_c << '\n';
    resfile << "v:1|0;\n";


    for (uint32_t i = toffset, tsz = test.size(); i < tsz; ++i) {
        Classify(train, test[i], is_fraud, not_is_fraud, 0.01F, cpus);
        resfile << test[i].is_fraud << ':' << is_fraud << '|' << not_is_fraud << ';' << '\n';
        resfile.flush();
    }

    resfile.close();

    return 0;
}
